# Reproducible build and test environment: docker build -t current-swimmer .
# Base image updates are picked up by Dependabot (docker).
FROM debian:13-slim

RUN apt-get update && apt-get install -y --no-install-recommends build-essential \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN make test && make plots

CMD ["./current-swimmer"]
