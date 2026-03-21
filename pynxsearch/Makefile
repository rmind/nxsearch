PROJ=	nxsearch

all:
	uv build

lint:
	uv run ruff check --select I --fix .
	uv run ruff format .

tests:
	uv pip install -e .
	uv run pytest tests

clean:
	rm -rf ./dist ./build $(PROJ).egg-info $(PROJ)/*.c $(PROJ)/*.so

.PHONY: all lint tests clean
