# Android launcher icon source art

The two 2048×2048 PNGs in this directory are the original full-resolution icon assets.
They are kept outside the Android app's resource/assets source sets so they do not add
full-resolution copies to the APK. Optimized launcher resources are generated under
`app/src/main/res`: the regular artwork is used for the icon, while the monochrome
asset is supplied as the themed-icon layer with a transparent silhouette mask.
