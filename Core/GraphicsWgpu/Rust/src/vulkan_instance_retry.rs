/// Backport of gfx-rs/wgpu#10979 for the pinned HAL. Retry only the optional
/// debug-utils extension, never the graphics backend or Vulkan API version.
pub fn create<T, E>(
    mut attempt: impl FnMut(bool) -> Result<T, E>,
    missing_debug_extension: impl FnOnce(&E) -> bool,
) -> Result<T, E> {
    match attempt(true) {
        Err(error) if missing_debug_extension(&error) => attempt(false),
        result => result,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn preserves_debug_utils_on_success() {
        assert_eq!(create(|debug| Ok::<_, ()>(debug), |_| panic!()), Ok(true));
    }

    #[test]
    fn retries_extension_failure_once_and_preserves_other_failures() {
        for retry in [true, false] {
            let mut attempts = Vec::new();
            let result = create(
                |debug| {
                    attempts.push(debug);
                    Err::<(), _>(if debug {
                        "first failure"
                    } else {
                        "second failure"
                    })
                },
                |_| retry,
            );
            if retry {
                assert_eq!(attempts, [true, false]);
                assert_eq!(result, Err("second failure"));
            } else {
                assert_eq!(attempts, [true]);
                assert_eq!(result, Err("first failure"));
            }
        }
    }

    #[test]
    fn returns_the_retried_vulkan_instance() {
        assert_eq!(
            create(|debug| if debug { Err(1) } else { Ok(2) }, |_| true),
            Ok(2)
        );
    }
}
