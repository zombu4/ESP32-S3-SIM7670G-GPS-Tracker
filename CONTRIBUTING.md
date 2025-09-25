# Contributing to ESP32-S3-SIM7670G GPS Tracker

Thank you for your interest in contributing to this project! This document provides guidelines for contributing to the ESP32-S3-SIM7670G GPS Tracker.

## Development Setup

### Prerequisites

1. **ESP-IDF v5.1+** - Install the Espressif IoT Development Framework
2. **Git** - For version control
3. **VS Code** with ESP-IDF extension (recommended)

### Local Development

1. **Clone the repository**:
   ```bash
   git clone https://github.com/yourusername/ESP32-S3-SIM7670G-4G.git
   cd ESP32-S3-SIM7670G-4G
   ```

2. **Set up configuration**:
   ```bash
   cp config.template.h main/config_user.h
   # Edit main/config_user.h with your settings
   ```

3. **Build the project**:
   ```bash
   idf.py set-target esp32s3
   idf.py build
   ```

## Architecture Guidelines

### Modular Design Principles

- **Separation of Concerns**: Each module handles a single responsibility
- **Clean Interfaces**: Use function pointers for module interfaces
- **Configuration Management**: All settings go through the centralized config system
- **Error Handling**: Comprehensive error checking and logging
- **Testing**: Modules should be independently testable

### Module Structure

When creating new modules, follow this structure:
```
main/modules/your_module/
├── your_module.h      # Interface definition
├── your_module.c      # Implementation
└── README.md          # Module documentation
```

### Code Style

- **Naming**: Use lowercase with underscores (`snake_case`)
- **Comments**: Document all public functions and complex logic
- **Logging**: Use ESP-IDF logging macros (`ESP_LOGI`, `ESP_LOGW`, etc.)
- **Includes**: Group system includes, then project includes
- **Constants**: Use `#define` for module-specific constants

## Making Changes

### Before You Start

1. **Check existing issues** to see if your change is already planned
2. **Create an issue** to discuss larger changes before implementing
3. **Fork the repository** and create a feature branch

### Development Workflow

1. **Create a branch**:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes**:
   - Follow the architecture guidelines
   - Add tests if applicable
   - Update documentation

3. **Test your changes**:
   ```bash
   idf.py build
   # Test on hardware if possible
   ```

4. **Commit your changes**:
   ```bash
   git add .
   git commit -m "feat: add your feature description"
   ```

### Commit Messages

Follow conventional commits format:
- `feat:` - New features
- `fix:` - Bug fixes
- `docs:` - Documentation changes
- `refactor:` - Code refactoring
- `test:` - Adding tests
- `chore:` - Build system, dependencies

## Pull Request Process

1. **Update documentation** if needed
2. **Ensure CI passes** - GitHub Actions will build your code
3. **Request review** from maintainers
4. **Address feedback** promptly

### Pull Request Template

```markdown
## Description
Brief description of changes

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Documentation update
- [ ] Refactoring

## Testing
- [ ] Code builds successfully
- [ ] Tested on hardware (if applicable)
- [ ] No regression in existing functionality

## Checklist
- [ ] Code follows project style guidelines
- [ ] Self-review completed
- [ ] Documentation updated
- [ ] Configuration template updated (if needed)
```

## Module Development

### Creating a New Module

1. **Create module directory**:
   ```bash
   mkdir -p main/modules/your_module
   ```

2. **Create interface header** (`your_module.h`):
   ```c
   #pragma once
   
   #include <stdbool.h>
   #include "../config.h"
   
   // Module interface
   typedef struct {
       bool (*init)(const your_module_config_t* config);
       bool (*deinit)(void);
       // Add other functions...
   } your_module_interface_t;
   
   const your_module_interface_t* your_module_get_interface(void);
   ```

3. **Implement module** (`your_module.c`):
   - Follow existing module patterns
   - Implement all interface functions
   - Add proper error handling and logging

4. **Update configuration**:
   - Add config structure to `config.h`
   - Update default config in `config.c`
   - Update `config.template.h` if user settings needed

5. **Update build system**:
   - Add source files to `main/CMakeLists.txt`

## Hardware Testing

### Required Hardware

- ESP32-S3-SIM7670G development board
- 4G SIM card with data plan
- GPS and 4G antennas
- 18650 battery (optional)

### Testing Guidelines

- Test core functionality before submitting PRs
- Document any hardware-specific behavior
- Include serial monitor output for debugging

## Documentation

### What to Document

- **Public APIs**: All module interfaces and functions
- **Configuration**: New config options and their effects
- **Hardware Changes**: Pin assignments, connections
- **Usage Examples**: How to use new features

### Documentation Style

- Use clear, concise language
- Include code examples where helpful
- Update README.md for user-facing changes
- Add inline code comments for complex logic

## Getting Help

- **Issues**: Create GitHub issues for bugs or questions
- **Discussions**: Use GitHub Discussions for general questions
- **ESP-IDF**: Refer to [official ESP-IDF documentation](https://docs.espressif.com/projects/esp-idf/)

## Code of Conduct

- Be respectful and constructive
- Help others learn and grow
- Focus on the technical merits
- Welcome newcomers and different perspectives

Thank you for contributing! 🚀