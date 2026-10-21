# Jak wydać nową wersję KettleLAN

1. Zmień numer wersji w `CMakeLists.txt`: `project(KettleLAN VERSION 0.2.0 …)`.
2. Upewnij się, że testy przechodzą: `ctest --test-dir build --output-on-failure`.
3. Zapisz zmiany i utwórz tag z tym samym numerem:

   ```sh
   git commit -am "Wersja 0.2.0"
   git tag v0.2.0
   git push origin main v0.2.0
   ```

4. GitHub Actions (workflow **Release**) zbuduje paczki `.deb` (Debian 12, Ubuntu 24.04) i AppImage,
   uruchomi testy, policzy sumy SHA256, doda atestację pochodzenia i opublikuje wydanie na stronie
   **Releases**. Postęp widać w zakładce **Actions**.

## Jednorazowa konfiguracja

- Prywatne repozytorium `maxiuz94/steamworks-sdk` z wydaniem `sdk-165` zawierającym oficjalny plik
  `steamworks_sdk_165.zip` od Valve.
- Sekret `SDK_REPO_TOKEN` w repozytorium `kettlelan`: token (fine-grained) z dostępem **tylko do odczytu**
  (`Contents: Read-only`) **tylko do** repozytorium `steamworks-sdk`.
- Przy zmianie wersji SDK zaktualizuj `cmake/steamworks-sdk.sha256`, nazwę pliku w `release.yml`
  i tag wydania w repozytorium `steamworks-sdk`.

## Próbne budowanie bez publikowania

Zakładka **Actions** → **Release** → **Run workflow**. Paczki pojawią się jako artefakty uruchomienia,
bez tworzenia wydania.
