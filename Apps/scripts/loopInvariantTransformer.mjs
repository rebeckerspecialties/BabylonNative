// Experimental build-time pass. Not enabled in the shipped bundle.
// Only lexical, non-coercing expressions are eligible. Property reads need a
// separate alias/effect proof; TypeScript readonly/private are not that proof.
export function createLoopInvariantTransformer(ts, program) {
    const checker = program.getTypeChecker();
    const report = { hoisted: [], rejectedProperties: [] };
    const facts = new Map();
    const operations = new Set([
        ts.SyntaxKind.EqualsEqualsEqualsToken, ts.SyntaxKind.ExclamationEqualsEqualsToken,
        ts.SyntaxKind.AmpersandAmpersandToken, ts.SyntaxKind.BarBarToken, ts.SyntaxKind.QuestionQuestionToken,
    ]);
    const unwrap = node => {
        while (ts.isParenthesizedExpression(node) || ts.isAsExpression(node) || ts.isTypeAssertionExpression(node) || ts.isNonNullExpression(node)) node = node.expression;
        return node;
    };
    function owner(node) {
        for (let parent = node.parent; parent; parent = parent.parent) if (ts.isFunctionLike(parent)) return parent;
    }
    function functionFacts(fn) {
        if (facts.has(fn)) return facts.get(fn);
        const result = { writes: new Set(), dynamic: false };
        function write(node) {
            node = unwrap(node);
            if (ts.isIdentifier(node)) result.writes.add(checker.getSymbolAtLocation(node));
            else if (ts.isArrayLiteralExpression(node)) node.elements.forEach(write);
            else if (ts.isObjectLiteralExpression(node)) node.properties.forEach(property => {
                if (ts.isShorthandPropertyAssignment(property)) result.writes.add(checker.getShorthandAssignmentValueSymbol(property));
                else if (ts.isPropertyAssignment(property)) write(property.initializer);
                else if (ts.isSpreadAssignment(property)) write(property.expression);
            });
            else if (ts.isSpreadElement(node)) write(node.expression);
            else if (ts.isBinaryExpression(node) && node.operatorToken.kind === ts.SyntaxKind.EqualsToken) write(node.left);
        }
        function scan(node) {
            // Include nested closures: an unknown callback can invoke one later.
            if (ts.isWithStatement(node) || (ts.isIdentifier(node) && (node.text === "eval" || node.text === "arguments"))) result.dynamic = true;
            if (ts.isBinaryExpression(node) && node.operatorToken.kind >= ts.SyntaxKind.FirstAssignment && node.operatorToken.kind <= ts.SyntaxKind.LastAssignment) write(node.left);
            if ((ts.isPrefixUnaryExpression(node) || ts.isPostfixUnaryExpression(node)) &&
                (node.operator === ts.SyntaxKind.PlusPlusToken || node.operator === ts.SyntaxKind.MinusMinusToken)) write(node.operand);
            if (ts.isForInStatement(node) || ts.isForOfStatement(node)) {
                if (!ts.isVariableDeclarationList(node.initializer)) write(node.initializer);
            }
            ts.forEachChild(node, scan);
        }
        scan(fn);
        facts.set(fn, result);
        return result;
    }
    function bindingIsStable(node, loop, fn, state) {
        const symbol = checker.getSymbolAtLocation(node);
        if (!symbol || state.writes.has(symbol)) return false;
        const declarations = symbol.declarations;
        if (!declarations || declarations.length !== 1) return false;
        const declaration = declarations[0];
        if (!ts.isIdentifier(declaration.name) || owner(declaration) !== fn) return false;
        if (ts.isParameter(declaration)) return true;
        if (!ts.isVariableDeclaration(declaration) || !declaration.initializer ||
            !(declaration.parent.flags & ts.NodeFlags.Const)) return false;
        const statement = declaration.parent.parent;
        if (!ts.isVariableStatement(statement) || !ts.isBlock(statement.parent)) return false;
        // The initializer must have executed before this loop on every path.
        for (let ancestor = loop; ancestor && ancestor !== fn; ancestor = ancestor.parent) {
            if (ancestor.parent === statement.parent) return statement.end <= ancestor.pos;
        }
        return false;
    }
    function invariant(node, loop, fn, state) {
        node = unwrap(node);
        if (ts.isIdentifier(node)) return bindingIsStable(node, loop, fn, state);
        if (ts.isLiteralExpression(node) && !ts.isRegularExpressionLiteral(node)) return true;
        if ([ts.SyntaxKind.TrueKeyword, ts.SyntaxKind.FalseKeyword, ts.SyntaxKind.NullKeyword].includes(node.kind)) return true;
        if (ts.isBinaryExpression(node)) return operations.has(node.operatorToken.kind) && invariant(node.left, loop, fn, state) && invariant(node.right, loop, fn, state);
        if (ts.isPrefixUnaryExpression(node)) return node.operator === ts.SyntaxKind.ExclamationToken && invariant(node.operand, loop, fn, state);
        if (ts.isTypeOfExpression(node)) return invariant(node.expression, loop, fn, state);
        if (ts.isConditionalExpression(node)) return invariant(node.condition, loop, fn, state) && invariant(node.whenTrue, loop, fn, state) && invariant(node.whenFalse, loop, fn, state);
        return false;
    }
    function worthHoisting(node) {
        let identifiers = 0, operators = 0;
        function scan(n) {
            if (ts.isIdentifier(n)) identifiers++;
            if (ts.isBinaryExpression(n) || ts.isPrefixUnaryExpression(n) || ts.isTypeOfExpression(n) || ts.isConditionalExpression(n)) operators++;
            ts.forEachChild(n, scan);
        }
        scan(node);
        return identifiers > 0 && operators > 0;
    }
    function location(node, source) {
        return { file: source.fileName, line: source.getLineAndCharacterOfPosition(node.getStart(source)).line + 1, expression: node.getText(source) };
    }
    return {
        report,
        before: [context => source => {
            const factory = context.factory;
            function visit(node) {
                if (!ts.isBlock(node)) return ts.visitEachChild(node, visit, context);
                const statements = [];
                for (const statement of node.statements) {
                    // Leave labeled loops, for-in/of, and single-statement scopes alone.
                    if (!ts.isForStatement(statement)) {
                        statements.push(ts.visitNode(statement, visit));
                        continue;
                    }
                    const fn = owner(statement);
                    const state = fn && functionFacts(fn);
                    if (!state || state.dynamic) {
                        statements.push(statement);
                        continue;
                    }
                    const replacements = new Map();
                    const declarations = [];
                    function move(n) {
                        if (ts.isFunctionLike(n)) return n;
                        if (ts.isPropertyAccessExpression(n) || ts.isElementAccessExpression(n)) {
                            report.rejectedProperties.push({ ...location(n, source), reason: "observable-read-or-mutation-not-proven-absent" });
                        }
                        if (invariant(n, statement, fn, state) && worthHoisting(n)) {
                            const key = n.getText(source);
                            let name = replacements.get(key);
                            if (!name) {
                                name = factory.createUniqueName("_loopInvariant");
                                replacements.set(key, name);
                                declarations.push(factory.createVariableDeclaration(name, undefined, undefined, n));
                                report.hoisted.push(location(n, source));
                            }
                            return name;
                        }
                        return ts.visitEachChild(n, move, context);
                    }
                    // Do not move initializer/update work or nested loop bindings.
                    const body = ts.visitNode(statement.statement, move);
                    if (declarations.length) statements.push(factory.createVariableStatement(undefined, factory.createVariableDeclarationList(declarations, ts.NodeFlags.Const)));
                    statements.push(factory.updateForStatement(statement, statement.initializer, statement.condition, statement.incrementor, body));
                }
                return factory.updateBlock(node, statements);
            }
            return ts.visitNode(source, visit);
        }],
    };
}
