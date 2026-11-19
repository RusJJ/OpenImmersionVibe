$NDKPath = (Get-Content "$PSScriptRoot/ndkpath.txt" -Raw).Trim()
Write-Output "NDK located at: $NDKPath"

$buildScript = "$NDKPath/build/ndk-build"
if ($IsWindows -or -not ($PSVersionTable.PSEdition -eq "Core")) {
    $buildScript += ".cmd"
}

Write-Output "[BUILD] Starting NDK..."
& $buildScript NDK_PROJECT_PATH=$PSScriptRoot APP_BUILD_SCRIPT=$PSScriptRoot/Android.mk NDK_APPLICATION_MK=$PSScriptRoot/Application.mk NDK_DEBUG=0 -j4
Write-Output "[BUILD] Done!"

Exit $LASTEXITCODE
