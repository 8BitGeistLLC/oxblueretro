import os
import subprocess
import sys
from pathlib import Path


def main():
    args = sys.argv[1:]
    if not args:
        print("Usage: python tools/runidf.py [idf.py] <command> [arguments]")
        print("Examples:")
        print("  python tools/runidf.py build")
        print("  python tools/runidf.py fullclean")
        return 2

    if Path(args[0]).name.lower() == "idf.py":
        args = args[1:]
        if not args:
            print("Usage: python tools/runidf.py [idf.py] <command> [arguments]")
            return 2

    project_dir = Path(__file__).resolve().parent.parent
    idf_dir = project_dir / "esp-idf"
    env = os.environ.copy()
    env["IDF_PATH"] = str(idf_dir)
    env["IDF_TOOLS_PATH"] = str(project_dir / "idf-tools")

    idf_tools = idf_dir / "tools" / "idf_tools.py"
    export = subprocess.run(
        [
            sys.executable,
            str(idf_tools),
            "--idf-path",
            str(idf_dir),
            "export",
            "--format=key-value",
        ],
        cwd=project_dir,
        env=env,
        check=True,
        capture_output=True,
        text=True,
    )
    for line in export.stdout.splitlines():
        if "=" not in line:
            continue
        name, value = line.split("=", 1)
        value = value.replace("%PATH%", env.get("PATH", ""))
        value = value.replace("$PATH", env.get("PATH", ""))
        env[name] = value

    python_env = Path(env["IDF_PYTHON_ENV_PATH"])
    python = python_env / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not python.is_file():
        raise FileNotFoundError(
            f"ESP-IDF Python environment not found at {python}; run tools/installidf.py first"
        )

    return subprocess.run(
        [str(python), str(idf_dir / "tools" / "idf.py"), *args],
        cwd=project_dir,
        env=env,
        check=False,
    ).returncode


if __name__ == "__main__":
    sys.exit(main())
