import sys
import yaml
import json

json.dump(yaml.load(sys.stdin), sys.stdout, indent=2)
