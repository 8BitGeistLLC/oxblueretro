import os
import subprocess
import sys
from pathlib import Path


def run_install_util(idf_dir, action, args, env):
    result = subprocess.run(
        [
            sys.executable,
            str(idf_dir / "tools" / "install_util.py"),
            "extract",
            action,
            *args,
        ],
        check=True,
        capture_output=True,
        text=True,
        env=env,
    )
    return result.stdout.strip()


def main():
    if any(arg in ("-h", "--help") for arg in sys.argv[1:]):
        print("Usage: python tools/installidf.py [ESP-IDF target ...] [--enable-*] [--disable-*]")
        print("Example: python tools/installidf.py esp32,esp32c3")
        return

    project_dir = Path(__file__).resolve().parent.parent
    idf_dir = project_dir / "esp-idf"
    tools_dir = project_dir / "idf-tools"

    env = os.environ.copy()
    env["IDF_PATH"] = str(idf_dir)
    env["IDF_TOOLS_PATH"] = str(tools_dir)

    args = sys.argv[1:]
    targets = run_install_util(idf_dir, "targets", args, env)
    features = run_install_util(idf_dir, "features", args, env)
    idf_tools = idf_dir / "tools" / "idf_tools.py"

    print(f"Installing ESP-IDF tools under {tools_dir}")
    subprocess.run(
        [
            sys.executable,
            str(idf_tools),
            "--idf-path",
            str(idf_dir),
            "install",
            f"--targets={targets}",
        ],
        cwd=project_dir,
        env=env,
        check=True,
    )
    subprocess.run(
        [
            sys.executable,
            str(idf_tools),
            "--idf-path",
            str(idf_dir),
            "install-python-env",
            f"--features={features}",
        ],
        cwd=project_dir,
        env=env,
        check=True,
    )
    print("Setup complete. Build with: python tools/runidf.py build")


if __name__ == "__main__":
    main()
