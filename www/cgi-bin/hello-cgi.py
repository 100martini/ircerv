import sys
import urllib.parse

data = sys.stdin.read()

params = urllib.parse.parse_qs(data)
name = params.get("name", ["stranger"])[0]

print("Content-Type: text/html\r\n")
print("\r\n")
print(f"<html><body><h1>Hello, {name}! 🥹</h1></body></html>")
