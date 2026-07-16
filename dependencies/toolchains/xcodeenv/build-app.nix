{
  stdenv,
  lib,
  composeXcodeWrapper,
}:
{
  name,
  src,
  sdkVersion ? "13.1",
  target ? null,
  configuration ? null,
  scheme ? null,
  sdk ? null,
  xcodeFlags ? "",
  release ? false,
  certificateFile ? null,
  certificatePassword ? null,
  provisioningProfile ? null,
  codeSignIdentity ? null,
  signMethod ? null,
  automaticProvisioning ? false,
  developmentTeam ? null,
  generateIPA ? false,
  generateXCArchive ? false,
  # Impure CI: signing material already installed by fastlane match into the
  # host keychain/profiles (see WAWONA_HOST_HOME). Skips Automatic signing.
  matchHostSigning ? false,
  enableWirelessDistribution ? false,
  installURL ? null,
  bundleId ? null,
  appVersion ? null,
  ...
}@args:

assert
  release
  ->
    (
      automaticProvisioning
      || matchHostSigning
      || (
        certificateFile != null
        && certificatePassword != null
        && provisioningProfile != null
        && signMethod != null
        && codeSignIdentity != null
      )
    );
assert enableWirelessDistribution -> installURL != null && bundleId != null && appVersion != null;
assert automaticProvisioning -> developmentTeam != null;

let
  _target = if target == null then name else target;
  # Archive/IPA requires -scheme; -target alone fails on modern Xcode with:
  #   "The flag -scheme is required when specifying -archivePath"
  _scheme =
    if scheme != null then
      scheme
    else if generateIPA || generateXCArchive then
      _target
    else
      null;
  targetFlag = lib.optionalString (_scheme == null) "-target ${_target}";

  _configuration =
    if configuration == null then if release then "Release" else "Debug" else configuration;

  _sdk =
    if sdk == null then
      if release then "iphoneos" + sdkVersion else "iphonesimulator" + sdkVersion
    else
      sdk;

  # Only for the certificateFile import path. matchHostSigning uses the
  # host/fastlane keychain — never create/delete a temp keychain there
  # (login.keychain is often absent on CI runners).
  deleteKeychain = ''
    if [ -n "''${keychainName:-}" ]; then
      security default-keychain -s login.keychain 2>/dev/null || true
      security delete-keychain "$keychainName" 2>/dev/null || true
    fi
  '';

  xcodewrapperFormalArgs = composeXcodeWrapper.__functionArgs or (builtins.functionArgs composeXcodeWrapper);
  xcodewrapperArgs = builtins.intersectAttrs xcodewrapperFormalArgs args;
  xcodewrapper = composeXcodeWrapper xcodewrapperArgs;

  extraArgs = removeAttrs args (
    [
      "name"
      "scheme"
      "xcodeFlags"
      "release"
      "certificateFile"
      "certificatePassword"
      "provisioningProfile"
      "codeSignIdentity"
      "signMethod"
      "automaticProvisioning"
      "developmentTeam"
      "generateIPA"
      "generateXCArchive"
      "matchHostSigning"
      "enableWirelessDistribution"
      "installURL"
      "bundleId"
      "version"
    ]
    ++ builtins.attrNames xcodewrapperFormalArgs
  );
in
stdenv.mkDerivation (
  {
    name = lib.replaceStrings [ " " ] [ "" ] name;
    impureEnvVars = [
      "WAWONA_HOST_HOME"
      "WAWONA_CODE_SIGN_STYLE"
      "WAWONA_CODE_SIGN_IDENTITY"
      "WAWONA_PROVISIONING_PROFILE_SPECIFIER"
      "WAWONA_XCODEBUILD_JOBS"
      # Paths only (contents never logged). Darwin daemon may not forward these;
      # match-host-signing-attrs also bakes the paths at --impure eval time.
      "WAWONA_DIST_P12_FILE"
      "WAWONA_DIST_P12_PASS_FILE"
      "WAWONA_PROFILES_DIR"
    ];
    buildPhase = ''
      export PATH=${xcodewrapper}/bin:$PATH
      export DEVELOPER_DIR="$(/usr/bin/xcode-select -p 2>/dev/null || true)"
      if [ -z "$DEVELOPER_DIR" ] || [ ! -x "$DEVELOPER_DIR/usr/bin/xcodebuild" ]; then
        if [ -n "''${XCODE_APP:-}" ] && [ -x "$XCODE_APP/Contents/Developer/usr/bin/xcodebuild" ]; then
          export DEVELOPER_DIR="$XCODE_APP/Contents/Developer"
        else
          XCODE_APP_CANDIDATE="$(ls -d /Applications/Xcode*.app 2>/dev/null | sort -V | tail -1 || true)"
          if [ -n "$XCODE_APP_CANDIDATE" ] && [ -x "$XCODE_APP_CANDIDATE/Contents/Developer/usr/bin/xcodebuild" ]; then
            export DEVELOPER_DIR="$XCODE_APP_CANDIDATE/Contents/Developer"
          else
            echo "ERROR: Could not resolve DEVELOPER_DIR. Set XCODE_APP or run xcode-select -s <Xcode.app>." >&2
            exit 1
          fi
        fi
      fi
      export PATH="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin:$DEVELOPER_DIR/usr/bin:$PATH"
      MATCH_KEYCHAIN=""
      keychainName=""
      # matchHostSigning: import staged Distribution P12 into a private keychain
      # under TMPDIR (readable by this build). Do not print pass/p12 contents.
      ${lib.optionalString (release && matchHostSigning) ''
        if [ -n "''${WAWONA_DIST_P12_FILE:-}" ] && [ -f "''${WAWONA_DIST_P12_FILE}" ] \
          && [ -n "''${WAWONA_DIST_P12_PASS_FILE:-}" ] && [ -f "''${WAWONA_DIST_P12_PASS_FILE}" ]; then
          export HOME="$TMPDIR/wawona-sign-home"
          export CFFIXED_USER_HOME="$HOME"
          mkdir -p "$HOME/Library/Keychains"
          mkdir -p "$HOME/Library/Developer/Xcode/DerivedData"
          mkdir -p "$HOME/Library/Developer/Xcode/Archives"
          mkdir -p "$HOME/Library/MobileDevice/Provisioning Profiles"
          mkdir -p "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"
          if [ -n "''${WAWONA_PROFILES_DIR:-}" ] && [ -d "''${WAWONA_PROFILES_DIR}" ]; then
            cp -f "''${WAWONA_PROFILES_DIR}/"*.mobileprovision \
              "$HOME/Library/MobileDevice/Provisioning Profiles/" 2>/dev/null || true
            cp -f "''${WAWONA_PROFILES_DIR}/"*.mobileprovision \
              "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles/" 2>/dev/null || true
          fi
          DIST_PASS="$(cat "''${WAWONA_DIST_P12_PASS_FILE}")"
          keychainName="wawona-dist-signing"
          MATCH_KEYCHAIN="$HOME/Library/Keychains/''${keychainName}-db"
          security delete-keychain "$keychainName" 2>/dev/null || true
          security create-keychain -p "$DIST_PASS" "$keychainName"
          security set-keychain-settings -t 3600 -u "$keychainName"
          security unlock-keychain -p "$DIST_PASS" "$keychainName"
          security list-keychains -d user -s "$MATCH_KEYCHAIN"
          security default-keychain -s "$MATCH_KEYCHAIN"
          # -A: allow codesign without UI prompt in headless CI
          security import "''${WAWONA_DIST_P12_FILE}" -k "$MATCH_KEYCHAIN" -P "$DIST_PASS" \
            -A -T /usr/bin/codesign -T /usr/bin/security -T /usr/bin/xcodebuild >/dev/null
          security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$DIST_PASS" "$MATCH_KEYCHAIN" >/dev/null
          RESOLVED="$(security find-identity -v -p codesigning "$MATCH_KEYCHAIN" 2>/dev/null \
            | awk '/Apple Distribution|iPhone Distribution|iOS Distribution/ { print $2; exit }')"
          if [ -z "$RESOLVED" ]; then
            echo "ERROR: Distribution identity missing after P12 import (no Apple Distribution in build keychain)." >&2
            exit 1
          fi
          export WAWONA_CODE_SIGN_IDENTITY="$RESOLVED"
          echo "Distribution identity ready (''${RESOLVED:0:8}…)"
          unset DIST_PASS
        fi
      ''}
      # Fallback: host fastlane keychain / writable HOME (legacy path)
      if [ -z "$MATCH_KEYCHAIN" ]; then
        if [ -n "''${WAWONA_HOST_HOME:-}" ] && [ -d "''${WAWONA_HOST_HOME}" ] \
          && mkdir -p "''${WAWONA_HOST_HOME}/.wawona-nix-write-test" 2>/dev/null; then
          rmdir "''${WAWONA_HOST_HOME}/.wawona-nix-write-test" 2>/dev/null || true
          export HOME="''${WAWONA_HOST_HOME}"
        else
          export HOME="$TMPDIR/home"
        fi
        export CFFIXED_USER_HOME="$HOME"
        mkdir -p "$HOME/Library/Developer/Xcode/DerivedData"
        mkdir -p "$HOME/Library/Developer/Xcode/Archives"
        mkdir -p "$HOME/Library/MobileDevice/Provisioning Profiles"
        mkdir -p "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"
        if [ -n "''${WAWONA_PROFILES_DIR:-}" ] && [ -d "''${WAWONA_PROFILES_DIR}" ]; then
          cp -f "''${WAWONA_PROFILES_DIR}/"*.mobileprovision \
            "$HOME/Library/MobileDevice/Provisioning Profiles/" 2>/dev/null || true
          cp -f "''${WAWONA_PROFILES_DIR}/"*.mobileprovision \
            "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles/" 2>/dev/null || true
        fi
        if [ -n "''${WAWONA_HOST_HOME:-}" ] \
          && [ -f "''${WAWONA_HOST_HOME}/Library/Keychains/fastlane_tmp_keychain-db" ]; then
          MATCH_KEYCHAIN="''${WAWONA_HOST_HOME}/Library/Keychains/fastlane_tmp_keychain-db"
        elif [ -f "$HOME/Library/Keychains/fastlane_tmp_keychain-db" ]; then
          MATCH_KEYCHAIN="$HOME/Library/Keychains/fastlane_tmp_keychain-db"
        fi
        if [ -n "$MATCH_KEYCHAIN" ]; then
          security list-keychains -d user -s "$MATCH_KEYCHAIN" 2>/dev/null || true
          security default-keychain -s "$MATCH_KEYCHAIN" 2>/dev/null || true
          security unlock-keychain -p "" "$MATCH_KEYCHAIN" 2>/dev/null || true
          security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "" "$MATCH_KEYCHAIN" 2>/dev/null || true
          echo "Using host match keychain"
          RESOLVED="$(security find-identity -v -p codesigning "$MATCH_KEYCHAIN" 2>/dev/null \
            | awk '/Apple Distribution|iPhone Distribution|iOS Distribution/ { print $2; exit }')"
          if [ -n "$RESOLVED" ]; then
            export WAWONA_CODE_SIGN_IDENTITY="$RESOLVED"
            echo "Distribution identity ready (''${RESOLVED:0:8}…)"
          fi
        fi
      fi

      ${lib.optionalString release ''
        ${lib.optionalString (!automaticProvisioning && !matchHostSigning) ''
          keychainName="$(basename $out)"
          security create-keychain -p "" $keychainName
          security default-keychain -s $keychainName
          security unlock-keychain -p "" $keychainName
          security import ${certificateFile} -k $keychainName -P "${certificatePassword}" -A
          security set-key-partition-list -S apple-tool:,apple: -s -k "" $keychainName
          PROVISIONING_PROFILE=$(grep UUID -A1 -a ${provisioningProfile} | grep -o "[-A-Za-z0-9]\{36\}")
          if [ ! -f "$HOME/Library/MobileDevice/Provisioning Profiles/$PROVISIONING_PROFILE.mobileprovision" ]
          then
              mkdir -p "$HOME/Library/MobileDevice/Provisioning Profiles"
              cp ${provisioningProfile} "$HOME/Library/MobileDevice/Provisioning Profiles/$PROVISIONING_PROFILE.mobileprovision"
          fi
          security find-identity -p codesigning $keychainName
        ''}
      ''}

      export CC="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
      export CXX="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
      export LD="$CC"

      ${lib.optionalString (lib.hasSuffix "simulator" _sdk) ''
        if [ "''${WAWONA_SKIP_IOS_SIMULATOR_PLATFORM_DOWNLOAD:-}" = "1" ]; then
          echo "Skipping xcodebuild -downloadPlatform iOS (WAWONA_SKIP_IOS_SIMULATOR_PLATFORM_DOWNLOAD=1)"
        else
          # actool CompileAssetCatalogVariant thinned needs a Simulator *runtime* whose build
          # matches the iphonesimulator SDK. Partial Xcode installs often error with:
          # "No simulator runtime version from [...] available to use with iphonesimulator SDK version ..."
          echo "Ensuring iOS Simulator platform/runtime is installed for SDK ${_sdk}..."
          env \
            -u NIX_CFLAGS_COMPILE \
            -u NIX_CXXFLAGS_COMPILE \
            -u NIX_LDFLAGS \
            -u NIX_LDFLAGS_BEFORE \
            -u NIX_CC_WRAPPER_FLAGS_SET \
            -u NIX_DONT_SET_RPATH \
            -u CFLAGS \
            -u CXXFLAGS \
            -u CPPFLAGS \
            -u LDFLAGS \
            -u CC \
            -u CXX \
            -u LD \
            xcodebuild -downloadPlatform iOS || {
            # CI runners often already have a matching runtime; Apple CDN flakes
            # and transient "Unable to connect to simulator" must not fail the
            # build when an iOS Simulator SDK/runtime is already present.
            if xcrun simctl list runtimes 2>/dev/null | grep -q 'iOS' \
              || xcodebuild -showsdks 2>/dev/null | grep -q 'iphonesimulator'; then
              echo "WARN: xcodebuild -downloadPlatform iOS failed; continuing with existing iOS Simulator SDK/runtime." >&2
            else
              echo "ERROR: xcodebuild -downloadPlatform iOS failed (network or Xcode issue)." >&2
              echo "Fix: Xcode → Settings → Components → install iOS Simulator for this Xcode, then retry." >&2
              echo "Or export WAWONA_SKIP_IOS_SIMULATOR_PLATFORM_DOWNLOAD=1 if runtimes already match." >&2
              exit 1
            fi
          }
        fi
      ''}

      env \
        -u NIX_CFLAGS_COMPILE \
        -u NIX_CXXFLAGS_COMPILE \
        -u NIX_LDFLAGS \
        -u NIX_LDFLAGS_BEFORE \
        -u NIX_CC_WRAPPER_FLAGS_SET \
        -u NIX_DONT_SET_RPATH \
        -u CFLAGS \
        -u CXXFLAGS \
        -u CPPFLAGS \
        -u LDFLAGS \
        -u CC \
        -u CXX \
        -u LD \
        xcodebuild ${targetFlag} -configuration ${_configuration} ${
        lib.optionalString (_scheme != null) "-scheme ${_scheme}"
      } -sdk ${_sdk} TARGETED_DEVICE_FAMILY="1, 2" ONLY_ACTIVE_ARCH=NO CONFIGURATION_TEMP_DIR=$TMPDIR CONFIGURATION_BUILD_DIR=$out ${
        lib.optionalString (generateIPA || generateXCArchive) "-archivePath \"${name}.xcarchive\" archive"
      } ${lib.optionalString (release && !automaticProvisioning && !matchHostSigning) ''PROVISIONING_PROFILE=$PROVISIONING_PROFILE OTHER_CODE_SIGN_FLAGS="--keychain $HOME/Library/Keychains/$keychainName-db"''} ${lib.optionalString (release && automaticProvisioning && !matchHostSigning) ''-allowProvisioningUpdates DEVELOPMENT_TEAM=${developmentTeam} CODE_SIGN_STYLE=Automatic''} ${lib.optionalString (release && matchHostSigning) ''DEVELOPMENT_TEAM=${developmentTeam} CODE_SIGN_STYLE=Manual CODE_SIGN_IDENTITY="''${WAWONA_CODE_SIGN_IDENTITY:-Apple Distribution}" PROVISIONING_PROFILE_SPECIFIER="''${WAWONA_PROVISIONING_PROFILE_SPECIFIER:-}" OTHER_CODE_SIGN_FLAGS="''${MATCH_KEYCHAIN:+--keychain $MATCH_KEYCHAIN}"''} ${xcodeFlags}

      ${lib.optionalString release ''
        ${lib.optionalString generateIPA ''
          cat > "${name}.plist" <<EOF
          <?xml version="1.0" encoding="UTF-8"?>
          <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
          <plist version="1.0">
          <dict>
              ${lib.optionalString (!automaticProvisioning && !matchHostSigning) ''
              <key>signingCertificate</key>
              <string>${codeSignIdentity}</string>
              <key>provisioningProfiles</key>
              <dict>
                  <key>${bundleId}</key>
                  <string>$PROVISIONING_PROFILE</string>
              </dict>
              <key>signingStyle</key>
              <string>manual</string>
              ''}
              ${lib.optionalString matchHostSigning ''
              <key>signingStyle</key>
              <string>manual</string>
              <key>teamID</key>
              <string>${developmentTeam}</string>
              <key>provisioningProfiles</key>
              <dict>
                  <key>${bundleId}</key>
                  <string>''${WAWONA_PROVISIONING_PROFILE_SPECIFIER:-match AppStore ${bundleId}}</string>
              </dict>
              ''}
              ${lib.optionalString (automaticProvisioning && !matchHostSigning) ''
              <key>signingStyle</key>
              <string>automatic</string>
              <key>teamID</key>
              <string>${developmentTeam}</string>
              ''}
              <key>method</key>
              <string>${
                if matchHostSigning then
                  "app-store"
                else if automaticProvisioning then
                  "development"
                else
                  signMethod
              }</string>
              ${lib.optionalString (signMethod == "enterprise" || signMethod == "ad-hoc") ''
                <key>compileBitcode</key>
                <false/>
              ''}
          </dict>
          </plist>
          EOF

          env \
            -u NIX_CFLAGS_COMPILE \
            -u NIX_CXXFLAGS_COMPILE \
            -u NIX_LDFLAGS \
            -u NIX_LDFLAGS_BEFORE \
            -u NIX_CC_WRAPPER_FLAGS_SET \
            -u NIX_DONT_SET_RPATH \
            -u CFLAGS \
            -u CXXFLAGS \
            -u CPPFLAGS \
            -u LDFLAGS \
            -u CC \
            -u CXX \
            -u LD \
            xcodebuild -exportArchive -archivePath "${name}.xcarchive" -exportOptionsPlist "${name}.plist" -exportPath $out ${lib.optionalString automaticProvisioning "-allowProvisioningUpdates"}

          mkdir -p $out/nix-support
          echo "file binary-dist \"$(echo $out/*.ipa)\"" > $out/nix-support/hydra-build-products

          ${lib.optionalString enableWirelessDistribution ''
            appname="$(basename "$(echo $out/*.ipa)" .ipa)"
            sed -e "s|@INSTALL_URL@|${installURL}?bundleId=${bundleId}\&amp;version=${appVersion}\&amp;title=$appname|" ${./install.html.template} > $out/''${appname}.html
            echo "doc install \"$out/''${appname}.html\"" >> $out/nix-support/hydra-build-products
          ''}
        ''}
        ${lib.optionalString generateXCArchive ''
          mkdir -p $out
          mv "${name}.xcarchive" $out
        ''}

        ${lib.optionalString (!automaticProvisioning && !matchHostSigning) ''
          ${deleteKeychain}
        ''}
        ${lib.optionalString matchHostSigning ''
          if [ -n "''${keychainName:-}" ]; then
            security delete-keychain "$keychainName" 2>/dev/null || true
          fi
        ''}
      ''}
    '';

    failureHook =
      lib.optionalString (release && !automaticProvisioning && !matchHostSigning) deleteKeychain
      + lib.optionalString (release && matchHostSigning) ''
        if [ -n "''${keychainName:-}" ]; then
          security delete-keychain "$keychainName" 2>/dev/null || true
        fi
      '';

    installPhase = "true";
  }
  // extraArgs
)
