> **Archived.** A one-time bookkeeping note from the original Python→C++20 conversion at
> the very start of the project. Purely historical.

# Migration From Python Starter

The project has been converted to C++20.

| Old concept | C++ replacement |
|---|---|
| `pyproject.toml` | `CMakeLists.txt` + `CMakePresets.json` |
| `.venv` / pip | native compiler + CMake/Ninja |
| `src/fsoc_mvp/*.py` | `include/fsoc/*.hpp` + `src/*.cpp` |
| `pytest` | CTest + native test executable |
| Ruff | compiler warnings + clang-format/clang-tidy |
| NumPy Vec3 | auditable native `Vec3` for Step 1 |
| OpenCV Python | OpenCV C++ when image simulation starts |
| future NumPy/SciPy matrices | Eigen in UKF/MPC phase |

Your previous command:

```bash
python -m pip install -e '.[dev]'
```

is no longer relevant. The equivalent C++ workflow is:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```
