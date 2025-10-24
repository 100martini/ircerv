import os
import sys

content_length = os.environ.get('CONTENT_LENGTH', 0)
if content_length:
    body = sys.stdin.read(int(content_length))
    print("Content-Type: text/plain")
    print()
    print(f"You sent: {body}")
else:
    print("Content-Type: text/plain")
    print()
    print("No data received")