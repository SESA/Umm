import time
from flask import Flask

print('hello... this is a python server test...')

time.gmtime(0)


app = Flask(__name__)

@app.route("/")
def hello():
	return "Hello World!"

@app.route("/init")
def init():
	return "FLASK: Should send init here..."

@app.route("/run")
def run():
	return "FLASK: Should send run here..."

if __name__ == "__main__":
    app.run(host='0.0.0.0', threaded=True)
