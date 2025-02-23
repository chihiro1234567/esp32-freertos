## Simple HTTP Server

```
> cd server
> python -m venv venv
> source venv/bin/activate

> pip install fastapi uvicorn requests
```

launch server

```
> uvicorn app:app --host 0.0.0.0 --port 8000 --reload
```

test server

```
> python test-client.py
Response: 1740300999
Response: 1740301000
:
```
