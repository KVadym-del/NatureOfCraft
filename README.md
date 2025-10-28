# NatureOfCraft

## Building the Builder

```bash
cc nob.c -o nob
```

> **_NOTE:_** If the nob.c is changed, just call `./nob` again to rebuild the builder.

### Clangd(LSP) Configuration

`compile_commands.json` can be created using ht efollowing command:

```bash
bear -- ./nob
```

## Requirements

- **fmt**
- **glfw**
- **vulkan**

## Building the Project

### Debug

```bash
./nob debug
```

or

```bash
./nob
```

### Release

```bash
./nob release
```
