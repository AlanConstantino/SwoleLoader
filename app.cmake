# App identity, version and update source. Renaming the app or pointing it at
# a new release repository only needs changes here. See RELEASING.md.

# Shown in the Homebrew Menu and inside the app.
set(APP_NAME "SwoleLoader")
set(APP_AUTHOR "AlanConstantino")

# Release version, as MAJOR.MINOR.PATCH. Release tags must match it, for
# example v0.1.6.
set(APP_VERSION 0.1.7)

# Install location on the SD card: sd:/switch/<APP_FOLDER>/<APP_NRO>.
# Settings, logs and reports go in sd:/config/<APP_FOLDER>.
# Existing installs keep their location when these change; updates replace
# whichever file is running.
set(APP_FOLDER "swoleloader")
set(APP_NRO "swoleloader.nro")

# GitHub repository that publishes releases, such as
# https://github.com/owner/repo. Leave empty to build without updates.
set(APP_UPDATE_REPO "https://github.com/AlanConstantino/SwoleLoader")
