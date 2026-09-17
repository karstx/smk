{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    utils.url = "github:numtide/flake-utils";
    sinowisp = {
      url = "github:karstx/sinowisp";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    sinodude = {
      url = "github:carlossless/sinodude";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, utils, sinowisp, sinodude }:
    utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };

        # uCsim (the SDCC 8051 simulator) patched so the SH68F90's USB interrupt
        # (vector 7 @ 0x3B) is a real, registered interrupt source - stock uCsim
        # only knows the classic 8051 vectors. Lets us drive usb_interrupt_handler
        # in simulation with faithful vectoring/stack/nesting.
        ucsim-sh68f90 = pkgs.stdenv.mkDerivation {
          pname = "ucsim-sh68f90";
          inherit (pkgs.sdcc) version;

          src = pkgs.sdcc.src;

          # bison/flex are required; the SDCC tarball does not ship the generated
          # parser/lexer.
          nativeBuildInputs = with pkgs; [ bison flex ];

          # Register the SH68F90 as a uCsim CPU variant (-t sh68f90).
          patches = [ ./tools/ucsim/sh68f90-register.patch ];

          postPatch = ''
            cp ${./tools/ucsim/sh68f90.cc}  sim/ucsim/src/sims/s51.src/sh68f90.cc
            cp ${./tools/ucsim/sh68f90cl.h} sim/ucsim/src/sims/s51.src/sh68f90cl.h
            flex -o sim/ucsim/src/core/cmd.src/cmdlex.cc \
                    sim/ucsim/src/core/cmd.src/cmdlex.l
          '';

          # Build only the mcs51 simulator (others, e.g. f8, fail to build and we
          # don't need them).
          configurePhase = ''
            runHook preConfigure
            cd sim/ucsim
            ./configure \
              --enable-avr-sim=no   --enable-f8-sim=no     --enable-i8048-sim=no \
              --enable-i8085-sim=no --enable-m6800-sim=no  --enable-m6809-sim=no \
              --enable-m68hc08-sim=no --enable-m68hc11-sim=no --enable-m68hc12-sim=no \
              --enable-mos6502-sim=no --enable-oisc-sim=no --enable-p1516-sim=no \
              --enable-pblaze-sim=no --enable-pdk-sim=no   --enable-rxk-sim=no \
              --enable-st7-sim=no   --enable-stm8-sim=no   --enable-tlcs-sim=no \
              --enable-xa-sim=no    --enable-z80-sim=no
            runHook postConfigure
          '';

          buildPhase = ''
            runHook preBuild
            make
            runHook postBuild
          '';

          # Install under a distinct name: nixpkgs' sdcc already ships a stock
          # `ucsim_51` (no USB patch), so a same-named binary would collide on PATH.
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp src/sims/s51.src/ucsim_51 $out/bin/ucsim-sh68f90
            runHook postInstall
          '';

          meta.description = "SDCC uCsim 8051 simulator patched with the SH68F90 USB interrupt source";
        };

      in
      {
        packages = {
          inherit ucsim-sh68f90;
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            sdcc
            meson
            ninja

            sinowisp.packages."${system}".default # flashing over the device's own ISP bootloader
            sinodude.packages."${system}".default  # flashing over ICP (the *_full_flash targets)
            clang-tools # for clang-format

            ucsim-sh68f90 # patched 8051 simulator (USB vector 7 + SIE model)
            python3       # USB simulator test suite (tests/)

            # host-side smk-console (tools/smk-console)
            cargo
            rustc
            clippy
            rustfmt

            # useful supplementary tools
            binutils # for objcopy
            uhubctl
          ];
        };
      }
    );
}
