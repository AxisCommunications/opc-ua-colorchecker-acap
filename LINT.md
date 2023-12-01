# Lint of code base

This repo uses the
[super-linter](https://github.com/super-linter/super-linter)
to check the code base syntax and format on different file types.

## Linters in GitHub Action

When you create a pull request, super-linter will automatically be run as a
[GitHub Action](https://github.com/features/actions). If any of the linters
give an error, this will be shown in the action connected to the pull request.

## Run super-linter locally

Before you create a pull request, you are encouraged to
[run the super-linter locally](https://github.com/github/super-linter/blob/main/docs/run-linter-locally.md)
which is possible since it is available as a container. Using
[Docker](https://www.docker.com/), a local run would be invoked by:

```sh
docker run --rm \
  -v "$PWD":/tmp/lint \
  -e RUN_LOCAL=true \
  --env-file .github/super-linter.env \
  ghcr.io/super-linter/super-linter:slim-v5
```

## Run super-linter interactively

Sometimes it is more convenient to run super-linter interactively. To do so
with Docker:

```sh
docker run -it --rm \
  -v "$PWD":/tmp/lint \
  -w /tmp/lint \
  --env-file .github/super-linter.env \
  --entrypoint /bin/bash \
  ghcr.io/super-linter/super-linter:slim-v5
```

Then from the container terminal, the following commands can lint the the code
base for different file types:

```sh
# Lint C code (format)
find "$PWD" \( -iname \*.c -or -iname \*.h \) -exec clang-format --dry-run --Werror --verbose {} +

# Lint Dockerfile files
hadolint $(find -type f -name Dockerfile*)

# Lint Dockerfile files (alternative command)
find -type f -name Dockerfile* -exec hadolint {} +

# Lint JSON files
eslint --no-eslintrc -c /action/lib/.automation/.eslintrc.yml --ext .json .

# Lint Markdown files
markdownlint .

# Lint YAML files
yamllint .
```
