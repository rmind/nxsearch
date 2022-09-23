#!/usr/bin/env python3

import sys
import asyncio
import logging
import os
import time
from glob import iglob
from multiprocessing import Process, Queue
from queue import Empty, Full

import aiohttp
import requests

BASE_URL = os.getenv("BASE_URL", "http://0.0.0.0:9200")
INDEX_NAME = os.getenv("INDEX_NAME", "test")
DATA_PATH = os.getenv("DATA_PATH", "/data/")
N_PROCS = int(os.getenv("N_PROCS", 8))


async def worker(queue):
    async with aiohttp.ClientSession(headers={}) as session:
        while True:
            file_path = queue.get(timeout=0.1)
            with open(file_path) as f:
                content = f.read()

            title = os.path.basename(file_path)

            resp = await session.post(
                url=f"{BASE_URL}/{INDEX_NAME}/_doc/{title}",
                headers={"Content-Type": "application/json"},
                json={"title": title, "content": content},
            )
            assert resp.status == 201


def runner(queue):
    try:
        asyncio.run(worker(queue))
    except Empty:
        return


def experiment(count: int):
    requests.delete(f"{BASE_URL}/{INDEX_NAME}")
    requests.put(f"{BASE_URL}/{INDEX_NAME}?pretty")

    queue = Queue()

    workers = [Process(target=runner, args=(queue,)) for _ in range(int(N_PROCS))]
    for w in workers:
        w.start()

    start = time.time()

    for filepath, _ in zip(iglob(f"{DATA_PATH}*"), range(count)):
        while True:
            try:
                queue.put(filepath, timeout=0.1)
            except Full:
                continue
            else:
                break

    for w in workers:
        w.join()

    elapsed = time.time() - start
    print(f"{count}\t{elapsed:0.2f}s\t{count/elapsed:0.2f}doc/s")


if __name__ == "__main__":

    chunks = [int(x) for x in sys.argv[1:]]
    chunks = chunks if len(chunks) else (10, 100, 1000, 1000)

    for chunk in chunks:
        experiment(chunk)
