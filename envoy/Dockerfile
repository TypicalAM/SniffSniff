# syntax=docker/dockerfile:1

FROM nixos/nix:2.23.0 AS builder

# Copy source
COPY . /tmp/build
WORKDIR /tmp/build

RUN nix --extra-experimental-features "nix-command flakes" --option filter-syscalls false build .#YariloEnvoy

# Copy the Nix store closure into a directory.
# entire set of Nix store values that we need for our build.
RUN mkdir /tmp/nix-store-closure
RUN cp -R $(nix-store -qR result/) /tmp/nix-store-closure

# Scratch image 
FROM scratch

WORKDIR /app

# Copy /nix/store
COPY --from=builder /tmp/nix-store-closure /nix/store
COPY --from=builder /tmp/build/result /app

# Entrypoint allows us to define the load balancer target via a cmdline arg
COPY envoy/envoy.yaml /app/envoy-original.yaml
COPY envoy/entrypoint.sh /app/entrypoint.sh

RUN chmod +x /app/entrypoint.sh

EXPOSE 8080
ENTRYPOINT ["/app/entrypoint.sh"]
CMD ["-c", "/app/envoy.yaml", "--log-path", "/app/envoy.log"]
