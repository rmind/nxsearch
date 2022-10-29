import argparse
import json
import re
from collections import defaultdict
import yaml



def parse_route_line(line: str):
    verb = re.match(r"routes:([^\s]+)\(", line).groups()[0]
    url = re.findall(r"\@/:.*\"", line)[0][1:-1]
    params = re.findall(r"function\((.*)", line)[0][:-1].split(", ")[1:]

    parameters = []
    for param_type, param in zip(re.finditer(r"\:[a-z]+", url), params):
        param_type = param_type.group(0)
        url = url.replace(param_type, f"{{{param}}}", 1)
        parameters.append(
            {
                "name": param,
                "in": "path",
                "required": True,
                "schema": {"type": param_type[1:]},
            }
        )

    return verb, url, parameters


def parse_route_comment(comment):
    try:
        yml = yaml.safe_load(comment)
    except yaml.scanner.ScannerError:
        yml = {}

    request_body = {}
    if "bodyContentType" in yml:
        request_body = {
            yml["bodyContentType"]: {
                "schema": {"type": "object", "example": "your doc text"}
            }
        }

    responses = {
        code: content or {}
        for code, content in yml.get("responses", {}).items()
    }

    description = yml.get("description", ""),

    return description, request_body, responses

def parse(input: str, output: str):
    """
    Get swagger inline: https://github.com/readmeio/swagger-inline
    """
    open_api = {
        "openapi": "3.0.2",
        "info": {"title": "Swagger NXSearch", "description": ""},
        "paths": {},
    }

    with open(input, "r") as fin:
        lua_code = fin.read()
        routes_data = re.finditer(
            r"routes:.*\n.*\[((=*)\[(.|\n)*?)\]\2\]",
            lua_code
        )
        components = re.search(
            r"--\[\[( |\n)*components:(.|\n| )*--\]\]",
            lua_code
        )

    routes = defaultdict(dict)
    for comment_and_param in routes_data:
        params = comment_and_param.group(0)
        comment = comment_and_param.group(1)[2:-4]

        verb, url, parameters = parse_route_line(params)
        description, request_body, responses = parse_route_comment(comment)


        routes[url][verb] = {
            "description":description,
            "parameters": parameters,
            "responses": responses,
            "requestBody": {"content": request_body},
        }

    open_api["paths"] = routes

    yml = {}
    if components:
        components = components.group()[4:-4]
        try:
            yml = yaml.safe_load(components)
        except yaml.scanner.ScannerError:
            yml = {}

    open_api["components"] = yml.get("components", {})

    with open(output, "w") as fout:
        json.dump(open_api, fout, indent=4)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=str, required=True)
    parser.add_argument("--output", type=str, required=True)
    args = parser.parse_args()

    parse(args.input, args.output)
