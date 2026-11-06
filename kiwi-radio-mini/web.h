#pragma once

// apMode = true: crea la WiFi "KiwiRadio-Setup" (portal cautivo) para configurar la red.
// apMode = false: la radio ya esta en tu WiFi; sirve la pagina en su IP y en kiwiradio.local
void webStart(bool apMode);
void webLoop();    // llamar en loop()
