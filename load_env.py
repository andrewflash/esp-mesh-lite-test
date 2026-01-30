Import("env")
from os.path import join, isfile

env_file = join(env.subst("$PROJECT_DIR"), ".env")
if isfile(env_file):
    with open(env_file) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                # Strip quotes if present
                if value.startswith('"') and value.endswith('"'):
                    value = value[1:-1]
                    env.Append(CPPDEFINES=[(key, env.StringifyMacro(value))])
                elif value.isdigit():
                    env.Append(CPPDEFINES=[(key, value)])
                else:
                    env.Append(CPPDEFINES=[(key, env.StringifyMacro(value))])
