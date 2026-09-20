"""PlatformIO adapter for private anchor BLE identity generation."""

import importlib.util
import os
from pathlib import Path
import subprocess


def configure(env, environ):
    """Generate the identity header before exposing its include directory."""
    project = Path(env.subst("$PROJECT_DIR")).resolve()
    build = Path(env.subst("$BUILD_DIR"))
    if not build.is_absolute():
        build = project / build
    build = build.resolve()
    private_build = project / ".pio" / "build"
    if not build.is_relative_to(private_build) or build == private_build:
        raise ValueError("Identity header requires the project's private .pio/build environment directory")
    output = build / "generated" / "anchor_identity_bootstrap.h"
    if not output.parent.resolve().is_relative_to(build):
        raise ValueError("Identity header directory must not escape the build directory")
    try:
        ignored = subprocess.run(["git", "check-ignore", "-q", "--", str(output)],
                                 cwd=project, capture_output=True)
    except OSError:
        raise ValueError("Cannot verify private build output is Git-ignored") from None
    if ignored.returncode != 0:
        raise ValueError("Identity header must be Git-ignored and untracked")

    identity = environ.get("BINRANGE_BLE_IDENTITY_FILE")
    if identity is not None:
        if not identity:
            raise ValueError("Explicit identity file path must not be empty")
        identity = Path(identity)
        if not identity.is_absolute():
            identity = project / identity

    spec = importlib.util.spec_from_file_location(
        "binrange_identity_generator", project.parent.parent / "scripts" / "anchor_identity_build.py")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    generator.generate_header(identity, output)
    env.Append(CPPPATH=[str(output.parent)])


if "Import" in globals():
    Import("env")
    configure(env, os.environ)
