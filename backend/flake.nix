{
  description = "WPA2 Decrypter & Packet Analyzer";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-23.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let pkgs = import nixpkgs { inherit system; };
      in with pkgs; rec {
        # Development shell
        devShell = mkShell {
          name = "yarilo";
          nativeBuildInputs = [ doxygen clang-tools gdb cmake ninja spdlog grpc libtins protobuf openssl libpcap aircrack-ng iw libnl ];
        };

        # Runtime package
        packages.Yarilo = (with pkgs; stdenv.mkDerivation {
          pname = "Yarilo";
          version = "0.8";
          allSrc = pkgs.fetchFromGitHub {
              owner = "TypicalAM";
              repo = "Yarilo";
              rev = "v0.8";
              hash = "sha256-bgvMa5bEm01lPqsHbOWkW9P4cjAmnekWYDtTP5PmlfQ=";
          };
          src = pkgs.fetchFromGitHub {
              owner = "TypicalAM";
              repo = "Yarilo";
              rev = "v0.8";
              hash = "sha256-bgvMa5bEm01lPqsHbOWkW9P4cjAmnekWYDtTP5PmlfQ=";
          } + "/backend";

          nativeBuildInputs = [ doxygen clang-tools gdb cmake ninja pkg-config spdlog grpc libtins protobuf openssl libpcap aircrack-ng iw libnl ];
          vendorHash = "sha256-bgvMa5bEm01lPqsHbOWkW9P4cjAmnekWYDtTP5PmlfQ=";
          patchPhase = ''
            cp $allSrc/protos/service.proto .
            sed -i 's|get_filename_component(hw_proto "..\/protos\/service.proto" ABSOLUTE)|get_filename_component(hw_proto "service.proto" ABSOLUTE)|g' CMakeLists.txt
            sed -i '/\/usr\/local\/include\/libnl3/a ${pkgs.libnl.dev}\/include\/libnl3' cmake/FindLibNL.cmake
          '';
          installPhase = ''
            mkdir -p $out/bin
            mv $TMP/backend/build/yarilo $out/bin
          '';

          meta = {
            description = "WPA2 Decrypter & Packet Analyzer";
            homepage = "https://github.com/TypicalAM/Yarilo";
            license = lib.licenses.mit;
            maintainers = with lib.maintainers; [ TypicalAM ];
          };
        });

        # Default package
        defaultPackage = packages.Yarilo;
      }
    );
}

