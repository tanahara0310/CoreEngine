# Copilot Instructions

## Project Guidelines
- When reporting code changes, verify the current file content and avoid claiming a function is wired if it is only defined but not used. Ensure accuracy by rechecking the actual file contents to avoid speculative responses.
- During refactoring, write explanatory comments for functions and processing steps in the code to enhance clarity and maintainability. Aim for a separation of concerns in the Scene view implementation to avoid responsibility concentration and ensure extensibility for future feature additions. Specifically, separate functionalities into distinct classes, with the SceneViewport acting as a management class to limit its responsibilities.
- Prohibit PowerShell-based project/file addition operations; created files are considered already added to the project.
- Avoid excessive use of macros in logging implementations to maintain code clarity and simplicity.