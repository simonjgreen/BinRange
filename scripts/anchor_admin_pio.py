"""PlatformIO adapter for private bootstrap-header generation."""

import importlib.util
import os
from pathlib import Path
import subprocess


def configure(env, environ):
    """Fail the build before adding include paths if provisioning is unsafe."""
    project = Path(env.subst("$PROJECT_DIR")).resolve()
    build = Path(env.subst("$BUILD_DIR"))
    if not build.is_absolute():
        build = project / build
    build = build.resolve()
    if not build.is_relative_to(project / ".pio" / "build"):
        raise ValueError("Admin header requires the project's private .pio/build directory")
    output = build / "generated" / "anchor_admin_bootstrap.h"
    if not output.parent.resolve().is_relative_to(build):
        raise ValueError("Admin header directory must not escape the build directory")
    try:
        ignored = subprocess.run(["git", "check-ignore", "-q", "--", str(output)],
                                 cwd=project, capture_output=True)
    except OSError:
        raise ValueError("Cannot verify private build output is Git-ignored") from None
    if ignored.returncode != 0:
        raise ValueError("Admin header must be Git-ignored and untracked")

    credential = environ.get("BINRANGE_ADMIN_FILE")
    if credential is not None:
        if not credential:
            raise ValueError("Explicit credential file path must not be empty")
        credential = Path(credential)
        if not credential.is_absolute():
            credential = project / credential

    spec = importlib.util.spec_from_file_location(
        "binrange_admin_generator", project.parent.parent / "scripts" / "anchor_admin_build.py")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    generator.generate_header(credential, output)
    env.Append(CPPPATH=[str(output.parent)])


# SCons supplies Import when this file is loaded as a pre extra_script. Normal
# Python imports (tests) have no build side effects.
if "Import" in globals():
    Import("env")
    configure(env, os.environ)
