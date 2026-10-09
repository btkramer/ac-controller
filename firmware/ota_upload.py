# Wires `pio run -e ota -t upload` to the HTTP /update endpoint. Credentials come from
# ../secrets.h (gitignored); target host defaults to 192.168.1.197 (ac-panel.lan does not resolve; consider a DHCP reservation), override with AC_HOST=<ip>.
import os, re
Import("env")
sec = open(os.path.join(env["PROJECT_DIR"], "..", "secrets.h")).read()
def val(k): return re.search(r'#define %s\s+"(.*)"' % k, sec).group(1)
host = os.environ.get("AC_HOST", "192.168.1.197")
env.Replace(UPLOADCMD='curl -sf --max-time 180 -u %s:%s -F "firmware=@$SOURCE" http://%s/update' % (val("OTA_USER"), val("OTA_PASS"), host))
