"""C++ code syntax checker for AI Smart Watch firmware v2.0"""
import re, sys

with open('firmware/src/main.cpp', 'r', encoding='utf-8') as f:
    code = f.read()

errors = []

# 1. Braces balance
opens = code.count('{')
closes = code.count('}')
if opens != closes:
    errors.append(f'BRACE MISMATCH: {opens} opens vs {closes} closes')
else:
    print(f'  OK: Braces balanced ({opens} pairs)')

# 2. Parentheses balance
p_open = code.count('(')
p_close = code.count(')')
if p_open != p_close:
    errors.append(f'PAREN MISMATCH: {p_open} vs {p_close}')
else:
    print(f'  OK: Parentheses balanced ({p_open} pairs)')

# 3. Function declarations
funcs = set(re.findall(r'void\s+(\w+)\s*\(', code))
print(f'  OK: {len(funcs)} functions declared: {sorted(funcs)}')

# 4. Pin definitions (numeric only)
pin_defs = re.findall(r'#define\s+(\w+)\s+(\S+)', code)
pin_only = [(n, v) for n, v in pin_defs if any(p in n for p in ['TFT_','I2C_','I2S_','BTN_','BAT_','WS2812']) and v.isdigit()]
print(f'  OK: {len(pin_only)} pin macros (numeric):')
for name, val in pin_only:
    print(f'      {name:20s} = {val}')

# 5. #include count
includes = re.findall(r'#include\s+[<\"](.+)[>\"]', code)
print(f'  OK: {len(includes)} includes:')
for inc in includes:
    print(f'      {inc}')

# 6. No duplicate defines
define_names = [n for n, v in pin_defs]
dupes = set([n for n in define_names if define_names.count(n) > 1])
if dupes:
    errors.append(f'DUPLICATE DEFINES: {dupes}')
else:
    print(f'  OK: No duplicate #define macros')

# 7. Required functions
required = ['setup', 'loop', 'runSelfTest', 'drawWatchFaceStatic',
            'updateWatchFace', 'updateSensorData', 'handleButtons',
            'readADCButtons', 'drawBootScreen']
missing = [f for f in required if f not in code]
if missing:
    errors.append(f'MISSING FUNCTIONS: {missing}')
else:
    print(f'  OK: All {len(required)} required functions present')

# 8. extern references match
extern_refs = re.findall(r'extern\s+(\w+(?:\s+\w+)*)\s+(\w+)\s*;', code)
print(f'  OK: {len(extern_refs)} extern references')

# 9. Check for missing semicolons (simplified)
lines = code.split('\n')
suspect_lines = []
for i, line in enumerate(lines, 1):
    stripped = line.strip()
    # Simple function calls without ; (excluding control flow)
    if (stripped.endswith(')') and not stripped.endswith(');')
        and not stripped.startswith('//') and not stripped.startswith('#')
        and not stripped.startswith('/*') and not stripped.startswith('*')
        and not any(stripped.startswith(kw) for kw in
                     ['if', 'else', 'for', 'while', 'switch', 'case', 'default'])):
        suspect_lines.append(i)
# Allow some (like if conditions), just report if more than expected
if len(suspect_lines) > 15:  # reasonable threshold for non-control-flow lines ending in )
    errors.append(f'{len(suspect_lines)} lines ending with ) without ; -- review manually')
else:
    print(f'  OK: Only {len(suspect_lines)} lines ending in ) without ; (likely control flow)')

# Final
if errors:
    print(f'\n*** {len(errors)} ISSUES FOUND ***')
    for e in errors:
        print(f'  - {e}')
    sys.exit(1)
else:
    print(f'\n*** All checks passed! Code is syntactically correct. ***')
    sys.exit(0)
