//! Reference spaces and rigid transforms (`XRReferenceSpace`, `XRRigidTransform`, `XRPose`).

/// `XRReferenceSpaceType`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ReferenceSpaceType {
    Viewer,
    Local,
    LocalFloor,
    BoundedFloor,
    Unbounded,
}

impl ReferenceSpaceType {
    pub fn as_str(self) -> &'static str {
        match self {
            ReferenceSpaceType::Viewer => "viewer",
            ReferenceSpaceType::Local => "local",
            ReferenceSpaceType::LocalFloor => "local-floor",
            ReferenceSpaceType::BoundedFloor => "bounded-floor",
            ReferenceSpaceType::Unbounded => "unbounded",
        }
    }
}

/// A rigid transform as position plus unit quaternion, column-major matrix on demand
/// (`XRRigidTransform.matrix`).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RigidTransform {
    pub position: [f32; 3],
    pub orientation: [f32; 4],
}

impl RigidTransform {
    pub const IDENTITY: RigidTransform = RigidTransform {
        position: [0.0, 0.0, 0.0],
        orientation: [0.0, 0.0, 0.0, 1.0],
    };

    /// Column-major 4x4 matrix, the layout `XRRigidTransform.matrix` and `XRView.projectionMatrix` use.
    pub fn to_matrix(&self) -> [f32; 16] {
        let [x, y, z, w] = self.orientation;
        let [tx, ty, tz] = self.position;
        let (xx, yy, zz) = (x * x, y * y, z * z);
        let (xy, xz, yz) = (x * y, x * z, y * z);
        let (wx, wy, wz) = (w * x, w * y, w * z);
        [
            1.0 - 2.0 * (yy + zz),
            2.0 * (xy + wz),
            2.0 * (xz - wy),
            0.0,
            2.0 * (xy - wz),
            1.0 - 2.0 * (xx + zz),
            2.0 * (yz + wx),
            0.0,
            2.0 * (xz + wy),
            2.0 * (yz - wx),
            1.0 - 2.0 * (xx + yy),
            0.0,
            tx,
            ty,
            tz,
            1.0,
        ]
    }

    /// The inverse transform (`XRRigidTransform.inverse`).
    pub fn inverse(&self) -> RigidTransform {
        let [x, y, z, w] = self.orientation;
        let conjugate = [-x, -y, -z, w];
        let [px, py, pz] = self.position;
        let rotated = rotate([-px, -py, -pz], conjugate);
        RigidTransform {
            position: rotated,
            orientation: conjugate,
        }
    }
}

fn rotate(v: [f32; 3], q: [f32; 4]) -> [f32; 3] {
    let [qx, qy, qz, qw] = q;
    let [vx, vy, vz] = v;
    // v' = v + 2w(q x v) + 2(q x (q x v))
    let (cx, cy, cz) = (qy * vz - qz * vy, qz * vx - qx * vz, qx * vy - qy * vx);
    let (ccx, ccy, ccz) = (qy * cz - qz * cy, qz * cx - qx * cz, qx * cy - qy * cx);
    [
        vx + 2.0 * (qw * cx + ccx),
        vy + 2.0 * (qw * cy + ccy),
        vz + 2.0 * (qw * cz + ccz),
    ]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn identity_matrix() {
        let m = RigidTransform::IDENTITY.to_matrix();
        for (i, v) in m.iter().enumerate() {
            let expected = if i % 5 == 0 { 1.0 } else { 0.0 };
            assert_eq!(*v, expected, "element {i}");
        }
    }

    #[test]
    fn inverse_undoes_translation_and_rotation() {
        let half = (std::f32::consts::FRAC_PI_4).sin();
        let t = RigidTransform {
            position: [1.0, 2.0, 3.0],
            orientation: [0.0, half, 0.0, half],
        };
        let inv = t.inverse();
        let p = rotate(rotate([0.5, -1.0, 2.0], t.orientation), inv.orientation);
        for (a, b) in p.iter().zip([0.5, -1.0, 2.0]) {
            assert!((a - b).abs() < 1e-5);
        }
        let back = {
            let moved = [t.position[0] + 0.0, t.position[1], t.position[2]];
            let r = rotate(moved, inv.orientation);
            [
                r[0] + inv.position[0],
                r[1] + inv.position[1],
                r[2] + inv.position[2],
            ]
        };
        for v in back {
            assert!(v.abs() < 1e-5);
        }
    }
}
