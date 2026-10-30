use std::path::PathBuf;

use anyhow::{bail, Context, Result};
use webxr_idl_gen::{dom_profile, generate, validate};

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    let (idl_dir, out_path) = match args.as_slice() {
        [_, idl_dir, out_path] => (PathBuf::from(idl_dir), PathBuf::from(out_path)),
        _ => bail!("usage: webxr-idl-gen <idl-dir> <output.wit>"),
    };

    let profile = dom_profile::Profile::no_dom();
    let generated = generate(&idl_dir, Some(&profile))?;

    if let Some(report) = &generated.profile_report {
        println!(
            "no-DOM profile: dropped {} definitions, {} members; cleared inheritance on {}",
            report.dropped_definitions.len(),
            report.dropped_members.len(),
            report.cleared_inheritance.join(", ")
        );
    }
    if !generated.placeholder_callbacks.is_empty() {
        println!(
            "placeholder resources for callbacks: {}",
            generated.placeholder_callbacks.join(", ")
        );
    }

    let wit_dir = out_path
        .parent()
        .context("output path has no parent directory")?;
    validate(&generated.wit, &wit_dir.join("deps"))?;
    std::fs::write(&out_path, &generated.wit)
        .with_context(|| format!("writing {}", out_path.display()))?;
    println!(
        "wrote {} ({} bytes)",
        out_path.display(),
        generated.wit.len()
    );
    Ok(())
}
