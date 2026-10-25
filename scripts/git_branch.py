"""
PlatformIO pre-build script: inject the version produced by scripts/version.py
into CROSSPOINT_VERSION for development environments.

Results in a version string like:  1.0.0-dev+05c6cf8
Release environments are unaffected; they set CROSSPOINT_VERSION in the ini.
"""

import os
import subprocess
import sys


def warn(msg):
    print(f'WARNING [git_branch.py]: {msg}', file=sys.stderr)


def get_version(project_dir):
    script = os.path.join(project_dir, 'scripts', 'version.py')
    try:
        result = subprocess.run(
            [sys.executable, script],
            text=True, stdout=subprocess.PIPE, cwd=project_dir
        )
    except OSError as e:
        warn(f'could not run {script}: {e}; version will be "0.0.0-unknown"')
        return '0.0.0-unknown'
    version = result.stdout.strip()
    if result.returncode != 0 or not version:
        warn(f'{script} failed (exit {result.returncode}); version will be "0.0.0-unknown"')
        return '0.0.0-unknown'
    # Strip characters that would break a C string literal
    return ''.join(c for c in version if c not in '"\\')


def inject_version(env):
    # Only applies to development environments; release envs set the
    # version via build_flags in platformio.ini and are unaffected.
    if env['PIOENV'] not in ('default', 'sticky'):
        return

    project_dir = env['PROJECT_DIR']
    version_string = get_version(project_dir)

    def add_version(build_env, node):
        # Only these translation units depend on the version. Keep library and
        # reader objects reusable when the version changes.
        if b'CROSSPOINT_VERSION' not in node.srcnode().get_contents():
            return node
        version_env = build_env.Clone()
        version_env.Append(CPPDEFINES=[('CROSSPOINT_VERSION', f'\\"{version_string}\\"')])
        return version_env.Object(node)

    env.AddBuildMiddleware(add_version)
    print(f'Tofu Reader build version: {version_string}')


# PlatformIO/SCons entry point — Import and env are SCons builtins injected at runtime.
# When run directly with Python (e.g. for validation), a lightweight fake env is used
# so the version logic can be exercised without a full build.
try:
    Import('env')           # noqa: F821  # type: ignore[name-defined]
    inject_version(env)     # noqa: F821  # type: ignore[name-defined]
except NameError:
    class _Env(dict):
        def AddBuildMiddleware(self, _):
            # Validation mode only reports the computed version.
            return None

    _project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    inject_version(_Env({'PIOENV': 'default', 'PROJECT_DIR': _project_dir}))
