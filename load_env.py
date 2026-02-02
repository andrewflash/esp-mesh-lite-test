Import("env")
from os.path import join, isfile

def load_env_file(filepath, defines):
    """Load environment variables from a file into defines dict.

    Values are stored as tuples: (value, is_string)
    - Quoted values ("...") are strings
    - Unquoted values are literals (integers, etc.)
    """
    if not isfile(filepath):
        return
    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                # Check if quoted (string) or unquoted (literal/integer)
                if value.startswith('"') and value.endswith('"'):
                    defines[key] = (value[1:-1], True)  # String: strip quotes
                else:
                    defines[key] = (value, False)  # Literal: keep as-is

# Collect all defines (later files override earlier ones)
defines = {}

# 1. Load base .env (shared config)
base_env = join(env.subst("$PROJECT_DIR"), ".env")
load_env_file(base_env, defines)

# 2. Load environment-specific .env.{envname} (overrides base)
env_name = env.subst("$PIOENV")
env_specific = join(env.subst("$PROJECT_DIR"), f".env.{env_name}")
load_env_file(env_specific, defines)

# Apply all defines
for key, (value, is_string) in defines.items():
    if is_string:
        env.Append(CPPDEFINES=[(key, env.StringifyMacro(value))])
    else:
        env.Append(CPPDEFINES=[(key, value)])
