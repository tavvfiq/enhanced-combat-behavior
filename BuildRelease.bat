@echo off
setlocal

echo ========================================
echo Building CombatAITree for Skyrim SE and AE
echo One DLL, VR disabled
echo ========================================
echo.

if exist dist RMDIR dist /S /Q

echo Configuring the SE+AE build...
xmake f -m releasedbg --skyrim_se=y --skyrim_ae=y --skyrim_vr=n
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to configure the SE+AE build
    pause
    exit /b 1
)

echo Generating project files...
xmake project -k vsxmake
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to generate project files
    pause
    exit /b 1
)

echo Building the universal DLL...
xmake
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to build CombatAITree
    pause
    exit /b 1
)

set "PLUGIN_DIR=build\SkyrimSEAE\skse\plugins"
if not exist "%PLUGIN_DIR%\CombatAITree.dll" set "PLUGIN_DIR=build\windows\x64\releasedbg"
if not exist "%PLUGIN_DIR%\CombatAITree.dll" (
    echo ERROR: CombatAITree.dll was not found in the expected build folders
    pause
    exit /b 1
)

for %%R in (SE AE) do (
    if not exist "dist\%%R\SKSE\Plugins" mkdir "dist\%%R\SKSE\Plugins"
    copy "%PLUGIN_DIR%\CombatAITree.dll" "dist\%%R\SKSE\Plugins\CombatAITree.dll" /Y >nul
    if exist "%PLUGIN_DIR%\CombatAITree.pdb" (
        copy "%PLUGIN_DIR%\CombatAITree.pdb" "dist\%%R\SKSE\Plugins\CombatAITree.pdb" /Y >nul
    )
    xcopy "package" "dist\%%R" /I /Y /E >nul
)

echo.
echo Build completed successfully.
echo The same SE+AE-compatible DLL is in dist\SE and dist\AE.
pause
