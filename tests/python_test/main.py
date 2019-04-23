import time
import json
from flask import Flask, request, Response, make_response, jsonify

print('XXXhello... this is a python server test...\n')

app = Flask(__name__)

time.gmtime(0)

@app.route("/")
def hello():
	return "Hello World!\n"

@app.route("/init", methods=["POST"])
def init():
	print("request: ", request)
	print("\nFLASK: Should send init here...\n")
	data = {"OK":True}
	res = make_response(jsonify(data))
	return res

@app.route("/run", methods=["POST"])
def run():
	print("request: ", request)
	print("FLASK: Should send run here...\n")
	data = {"OK":True}
	res = make_response(jsonify(data))
	return res

if __name__ == "__main__":
    app.run(host='0.0.0.0', threaded=False)
