FROM  python:3.10-alpine

WORKDIR /app

COPY ./src/requeriments.txt .
RUN pip install -r requeriments.txt
COPY ./src/test.py .

CMD ["python", "test.py"]
