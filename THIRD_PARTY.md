# Third-party code and notices

Enhanced Combat Behavior is licensed under the GNU General Public License v3.0 or later (see `LICENSE`, shipped as `LICENSE.txt`).
It statically links CommonLibSSE-NG, which is GPL-3.0-or-later, so the built DLL is a GPL-3.0 work.
Source code: https://github.com/tavvfiq/enhanced-combat-behavior
The parts below come from or follow MIT-licensed projects; their notices are kept here as the MIT
license requires.

## EnhancedCombatAI (MIT)

This mod is a fork of EnhancedCombatAI ("Enhanced Combat AI", `CombatAI-NG.dll`). Its decision code
(state observers, decision matrix, humanizer skill rolls, config and menu) is the base of this project.

> MIT License
>
> Copyright (c) 2026 EnhancedCombatAI Contributors
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
> associated documentation files (the "Software"), to deal in the Software without restriction,
> including without limitation the rights to use, copy, modify, merge, publish, distribute,
> sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or
> substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
> NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
> NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
> DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
> OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## SCAR - Skyrim Combos AI Revolution (MIT, max-su-2019)

`src/SCARData.cpp` reads `SCAR_ActionData` annotations with SCAR 2.0.1's format and range rules, and
the combo follow-up starts attacks the way SCAR's attack-data path does (CombatAnimation with the
attack event). Source: https://github.com/max-su-2019/SCAR (commit e62b63a). MIT License,
Copyright (c) max-su-2019; the permission notice above applies.

## Combat Pathing Revolution (MIT, max-su-2019)

The behavior-tree node layouts and Address Library IDs in `include/TreeNodes.h` come through
FenixCombatAI-NG, which adapted the `CombatBehaviorTreeControl` / `CombatBehaviorTreeNode` layouts from
https://github.com/max-su-2019/CombatPathingRevolution. MIT License, Copyright (c) 2022 max-su-2019;
the permission notice above applies.

## Precision API (MIT, Ersh)

`include/PrecisionAPI.h` is Precision's public modder API header
(https://github.com/ersh1/Precision/blob/main/src/PrecisionAPI.h). MIT License, Copyright (c) 2023
Ersh; the permission notice above applies.

## SKSE Menu Framework API header

`include/API/SKSEMenuFramework.h` is an older revision of the SKSE Menu Framework 3 mod API header
(SKSE Menu Framework 3 by QTR-Modding, https://github.com/QTR-Modding/SKSE-Menu-Framework-3). This copy
came bundled with StyyxUtils (GPL-3.0), which this project no longer uses otherwise. The current
official header is published at https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API under the
GNU LGPL v2.1; the framework's README states the header's license does not require mods using it to
share their license. Both licenses are compatible with this project's GPL-3.0-or-later.

## Libraries

- CommonLibSSE-NG (https://github.com/alandtse/CommonLibSSE-NG): GPL-3.0-or-later with exceptions,
  originally MIT.
- spdlog (https://github.com/gabime/spdlog): MIT.
- SimpleIni (https://github.com/brofield/simpleini): MIT.
- JSON for Modern C++ (https://github.com/nlohmann/json, v3.12.0): MIT, Copyright (c) 2013-2025 Niels Lohmann.
