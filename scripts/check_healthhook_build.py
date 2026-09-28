"""Check source integration before Gradle; does not replace a native build."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def check(root):
    failures = []
    required = {
        "module/src/main/cpp/health_hook.inc": ["DobbyInstrument"],
        "module/src/main/cpp/il2cpp_dump.cpp": ['#include "health_hook.inc"', "health_hook::command"],
        "module/src/main/cpp/CMakeLists.txt": [
            "third_party/Dobby", "add_subdirectory",
            "target_include_directories(${MODULE_NAME} PRIVATE ${dobby_SOURCE_DIR}/include)",
            "target_link_libraries(${MODULE_NAME} log dobby_static)",
        ],
        "module/build.gradle": ['abiFilters "arm64-v8a"', "targets(moduleLibraryName)"],
        "module/src/main/cpp/third_party/Dobby/include/dobby.h": ["DobbyInstrument"],
        "module/src/main/cpp/third_party/Dobby/CMakeLists.txt": ["add_library(dobby_static STATIC"],
        "module/src/main/cpp/third_party/Dobby/LICENSE": [],
        "module/src/main/cpp/third_party/Dobby/source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_bridge_arm64.asm": [],
        "module/src/main/cpp/third_party/Dobby/source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_trampoline_arm64.asm": [],
    }
    for relative, markers in required.items():
        path = root / relative
        if not path.is_file():
            failures.append(f"Missing file: {relative}")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for marker in markers:
            if marker not in text:
                failures.append(f"Outdated/incomplete file: {relative}; missing {marker!r}")
    return failures


if __name__ == "__main__":
    errors = check(ROOT)
    if errors:
        print("Healthhook build inputs are incomplete:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        print("Upload module/build.gradle, module/src/main/cpp/CMakeLists.txt, "
              "the hook sources, and the entire third_party/Dobby directory together.", file=sys.stderr)
        sys.exit(1)
    print("Healthhook source integration OK: ARM64 + Dobby include/static link. Native compilation still required.")
