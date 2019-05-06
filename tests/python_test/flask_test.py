import os
import time
import json
from flask import Flask, request, Response, make_response, jsonify

print('hello... this is a python server test...\n')

app = Flask(__name__)

time.gmtime(0)

code = ''
args = ''

@app.route("/init", methods=["POST"])
def init():
	print("\nFLASK: init here...\n")
	global code
	global filename
	global binary

	code = request.get_json()['value']['code']
	print("\nFLASK: code: ", code)

	try:
		fn = compile(code, filename='mycode.py', mode='exec')
		print("\nFLASK: fn: ", fn)
		data = {"OK":True}
	except Exception:
		print("\nFLASK: could not compile incoming code..\n")
		data = {"OK":False}

	res = make_response(jsonify(data))
	return res

@app.route("/run", methods=["POST"])
def run():
	print("FLASK: run here...\n")
	exec(code)

	data = {"OK":True}
	res = make_response(jsonify(data))
	return res

if __name__ == "__main__":
    app.run(host='0.0.0.0', threaded=False)
