name: Build Geode Mod

on:
  push:
    branches:
      - main
      - master
  pull_request:
  workflow_dispatch:

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        config:
          - name: Windows
            os: windows-latest
            target: win
          - name: Android (64-bit)
            os: ubuntu-latest
            target: android64
          - name: Android (32-bit)
            os: ubuntu-latest
            target: android32
          - name: macOS
            os: macos-latest
            target: mac
          - name: iOS
            os: macos-latest
            target: ios

    name: Build ${{ matrix.config.name }}
    runs-on: ${{ matrix.config.os }}

    steps:
      - name: Checkout Code
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Build Geode Mod
        uses: geode-sdk/build-geode-mod@main
        with:
          target: ${{ matrix.config.target }}

  combine:
    name: Combine Artifacts
    runs-on: ubuntu-latest
    needs: build
    steps:
      - name: Combine Builds
        uses: geode-sdk/build-geode-mod/combine@main
