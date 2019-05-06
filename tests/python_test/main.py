import os
import time
import json
from flask import Flask, request, Response, make_response, jsonify

print('hello... this is a python server test...\n')

app = Flask(__name__)

time.gmtime(0)

code = ''
args = ''
fn = ''

global_context = {}

@app.route("/init", methods=["POST"])
def init():
	global code
	global fn
	global global_context

	code = request.get_json()['value']['code']
	print("\nFLASK: code: ", code)

	try:
		fn = compile(code, filename='mycode.py', mode='exec')
		exec(fn, global_context)
		data = {"OK":True}
		print("\nFLASK: init successful..")

	except Exception:
		data = {"OK":False}
		print("\nFLASK: could not compile incoming code..\n")

	res = make_response(jsonify(data))
	return res


@app.route("/run", methods=["POST"])
def run():
	global args

	args = request.get_json()['value']['args']
	print("\nFLASK: args: ", args)

	try:
		global_context['param'] = args
		exec('fun = %s(param)' % 'main', global_context)
		result = global_context['fun']
		data = {"OK":True,"result":result}
		print("\nFLASK: run successful..")

	except Exception:
		data = {"OK":False}
		print("FLASK: could not run code with incoming args...\n")

	res = make_response(jsonify(data))
	return res

if __name__ == "__main__":
    app.run(host='0.0.0.0', threaded=False)
