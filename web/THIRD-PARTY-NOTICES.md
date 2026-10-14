# Web Workspace Notices

The optional C++ JSON bridge uses nlohmann/json 3.11.3, by Niels Lohmann,
under the MIT license. Its source archive is SHA256-pinned in CMake and
its license is included in [licenses/json-LICENSE.MIT](licenses/json-LICENSE.MIT).

The browser reuses Lucide 0.468.0 PNG icons from the desktop asset set.
Lucide is distributed under the ISC license, including portions originating
from Feather under MIT. The complete upstream notice is retained at
[../gui/licenses/Lucide-LICENSE.txt](../gui/licenses/Lucide-LICENSE.txt).

No Qt libraries are linked into the web bridge. Python and browser runtimes
are supplied by the user, not bundled by this implementation. The web app
does not change the repository's still-pending project license decision or
clear a public binary release.