# Agnès reference core

Unmodified `agnes.c`, `agnes.h` and `LICENSE` from [kgabis/agnes](https://github.com/kgabis/agnes), commit `0e4220b084c467e39c04805d955e78c463feadd0`. Licensed under MIT; see the accompanying LICENSE and source notices. Git blob hashes are respectively `8ca532a5a5a81afa688abbfc23b32ddc9ace55f4`, `669390203762cb250d68056589c3105f4cb4c32b`, and `f98da07b974eb353bbe9bd577e821ee2105135d2`.

Used only by the bounded reference test, with a generated fixture and an optional caller-supplied NROM. It is not a public backend and is not linked into BootHush's CLI or NES library. The upstream core does not implement audio. These checks are smoke comparisons, not proof of hardware accuracy or compatibility.
