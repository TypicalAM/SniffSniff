{
  description = "WPA2 Decrypter & Packet Analyzer";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let pkgs = import nixpkgs { inherit system; };
      in with pkgs; rec {
        # Development shell
        devShell = mkShell {
          name = "yarilo";
          nativeBuildInputs = [ doxygen clang-tools gdb cmake ninja spdlog grpc libtins protobuf openssl libpcap aircrack-ng iw libnl nodejs protobuf sqlite go ];
        };

        # Runtime package
        packages.Yarilo = pkgs.callPackage ./backend { };
        packages.YariloBattery = (pkgs.callPackage ./backend { }).overrideAttrs (oldAttrs: rec {
          cmakeFlags = oldAttrs.cmakeFlags or [ ] ++ [ "-DYARILO_BATTERY_SUPPORT=ON" ];
        });
        packages.YariloFrontend = pkgs.callPackage ./frontend { };
        packages.YariloEnvoy = pkgs.callPackage ./envoy { };

        # Default package
        defaultPackage = packages.Yarilo;
      }
    );
}
