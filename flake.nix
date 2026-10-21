{
  description = "CC3069 Proyecto 02 - Busqueda de claves AES-128 con Open MPI";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "aes-busqueda";
          version = "0.1.0";
          src = self;

          nativeBuildInputs = [ pkgs.pkg-config pkgs.mpi ];
          buildInputs = [ pkgs.openssl pkgs.mpi ];

          makeFlags = [ "PREFIX=$(out)" ];
          enableParallelBuilding = true;
        };
      });

      apps = forAllSystems (pkgs:
        let
          pkg = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
          app = name: { type = "app"; program = "${pkg}/bin/${name}"; };
        in
        {
          default = app "busqueda_clave_aes_secuencial_mejorado";
          original = app "busqueda_clave_aes_secuencial";
          mejorado = app "busqueda_clave_aes_secuencial_mejorado";
        });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.default ];
          packages = [ pkgs.gnumake pkgs.gdb pkgs.valgrind pkgs.clang-tools ];
          shellHook = ''
            echo "Proyecto 02 - AES-128 + Open MPI"
            echo "  make            compila todo en bin/"
            echo "  make help       ver objetivos disponibles"
          '';
        };
      });
    };
}
