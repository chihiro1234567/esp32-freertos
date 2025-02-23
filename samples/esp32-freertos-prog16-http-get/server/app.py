from fastapi import FastAPI
import time

# uvicorn app:app --host 0.0.0.0 --port 8000 --reload

app = FastAPI()


@app.get("/")
def read_root():
    return "%s" % (int(time.time()))
