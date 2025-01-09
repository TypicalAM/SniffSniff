{ pkgs, lib }:

pkgs.envoy.overrideAttrs (oldAttrs: rec {
  bazelBuildFlags = oldAttrs.bazelBuildFlags ++ [
    "--define=tcmalloc=gperftools"
  ];
})
