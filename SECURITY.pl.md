# Bezpieczeństwo KettleLAN

[English](SECURITY.md) · **Polski**

Ten dokument opisuje, co KettleLAN chroni, czego nie chroni i jak zgłosić problem.

## Zgłaszanie luk

Jeśli znajdziesz lukę, **nie zakładaj publicznego zgłoszenia**. Użyj prywatnego zgłoszenia na GitHubie:
zakładka **Security → Report a vulnerability** w repozytorium.

## Kto co widzi

| Kto | Co widzi |
|-----|----------|
| Osoba postronna (dostawca internetu, to samo Wi-Fi) | Tylko zaszyfrowany ruch UDP Steama (AES-GCM, tożsamość potwierdzona certyfikatami Steam). Bez treści gier i bez adresów `100.x.x.x`. |
| Członek Twojej sieci, **„Ukryj mój adres IP” włączone** (domyślnie) | Twój nick Steam i adres `100.x.x.x`. Ruch idzie przez relay Valve, więc **nie poznaje Twojego publicznego IP**. |
| Członek Twojej sieci, ukrywanie wyłączone | Jak wyżej, ale przy połączeniu bezpośrednim także Twój publiczny adres IP. |
| Valve | Że korzystasz z sieci Steam w aplikacji Spacewar (App ID 480) i z kim się łączysz. |

## Ochrona komputera

- **Program nie działa jako root.** Uprawnienia administratora ma tylko skrypt `kettlelan-helper`,
  który umie wyłącznie utworzyć lub usunąć kartę `kettle0` i ściśle sprawdza argumenty. Hasło zbiera
  systemowe okno polkit; w terminalu bez pulpitu (np. przez SSH) KettleLAN prosi o nie przez `sudo`.
- **„Chroń usługi tego komputera”** (domyślnie włączone): pakiety od członków sieci do SSH, udostępniania
  plików (SMB, NFS, AFP), zdalnego pulpitu (RDP, VNC), baz danych, paneli administracyjnych, KDE Connect,
  Steam Remote Play itp. są odrzucane, zanim dotrą do systemu. Odrzucane są też protokoły inne niż
  TCP/UDP/ICMP oraz podejrzane fragmenty IP (RFC 1858). Pełna lista: [`src/core/filter.cpp`](src/core/filter.cpp).
- Przy włączonej ochronie Twój system **nie rozgłasza** w sieci KettleLAN swoich usług (mDNS/Avahi,
  NetBIOS, SSDP, KDE Connect, Spotify…), które zdradzają nazwę komputera i udostępniane zasoby.
- **Brak dostępu do sieci domowej.** Przyjmowane są tylko pakiety adresowane do Twojego adresu
  `100.x.x.x` (lub rozgłoszeniowe). KettleLAN nie przekazuje ruchu dalej i nie zmienia trasy internetu –
  przez `kettle0` idzie wyłącznie zakres `100.64.0.0/10`.
- **Tylko zweryfikowani członkowie.** Połączenia Steam przyjmowane są wyłącznie od osób z lobby sieci,
  a pakiety tylko od tych, które udowodniły znajomość klucza z kodu – i tylko z ich własnego adresu.
- **Tekst od innych osób** (nicki, nazwy sieci) jest oczyszczany ze znaków sterujących i wyświetlany
  jako zwykły tekst – bez HTML, więc nie da się nim np. wczytać obrazka z obcego serwera.
- Plik z zapisanymi sieciami i dziennik są dostępne tylko dla Twojego konta (uprawnienia `600`).

## Windows

- Wintun wymaga uprawnień administratora do utworzenia karty **i** do przesyłania pakietów, dlatego na Windowsie
  cały program działa jako administrator (monit UAC przy starcie). To rozwiązanie tymczasowe – w planach jest
  osobna usługa systemowa dla karty, a interfejs będzie działał jako zwykły użytkownik.
- `wintun.dll` jest ładowany wyłącznie z katalogu programu (ochrona przed podstawieniem biblioteki); dołączony plik
  to oficjalna wersja podpisana przez WireGuard LLC, sprawdzana sumą SHA256 przy budowaniu.
- Reguła Zapory Windows wpuszcza ruch przychodzący **tylko na karcie KettleLAN i tylko z adresów `100.64.0.0/10`**.
  Sieć KettleLAN zostaje siecią „Publiczną”, więc Windows nie udostępnia w niej plików ani drukarek, a filtr
  KettleLAN blokuje usługi komputera tak jak na Linuksie.

## Kod sieci

- Kod (`KTL-XXXX-XXXX-XXXX`, 60 bitów) działa jak hasło. Bez niego nie da się znaleźć lobby sieci
  (wyszukiwanie odbywa się po skrócie całego kodu) ani dołączyć do niej.
- Nie ma „właściciela”, który może kogoś wyrzucić. Jeśli kod trafi do niewłaściwej osoby, utwórz nową
  sieć i wyślij nowy kod tylko zaufanym osobom.

## Czego KettleLAN nie chroni

- Członkowie sieci są dla siebie jak komputery w jednym LAN-ie: widzą ruch gier, który do nich trafia,
  i mogą łączyć się z portami gier na Twoim komputerze. Wpuszczaj do sieci tylko zaufane osoby.
- Ochrona usług opiera się na liście portów. Usługa na nietypowym porcie (np. własny serwer na 8123)
  jest dostępna dla członków sieci – zamknij ją albo zablokuj w firewallu dla karty `kettle0`.
- Instalacja przez `scripts/install-local.sh` umieszcza pomocnika w katalogu domowym. Złośliwy program
  działający na Twoim koncie mógłby go podmienić. **Paczka `.deb` tego problemu nie ma**: pomocnik leży
  w `/usr/lib/kettlelan/` (własność roota), a okno hasła ma własny opis (reguła polkit
  `io.github.maxiuz94.kettlelan.helper`).
- **AppImage** to jeden plik należący do Ciebie – ma ten sam poziom zaufania co każdy inny program, który
  uruchamiasz ze swojego konta. Przed utworzeniem karty pomocnik jest kopiowany do świeżego, prywatnego
  katalogu (`0700`), bo root nie ma dostępu do wnętrza zamontowanego pliku AppImage.

## Skąd pochodzą gotowe pliki

- Paczki buduje GitHub Actions z publicznego kodu ([`.github/workflows/release.yml`](.github/workflows/release.yml)),
  a log budowania jest publiczny.
- Przy każdym wydaniu: sumy `SHA256SUMS.txt` i atestacja pochodzenia –
  `gh attestation verify NAZWA_PLIKU -R maxiuz94/kettlelan` potwierdza, że plik powstał w tym
  repozytorium z konkretnego commita.
- Narzędzia do budowania AppImage (linuxdeploy) są pobierane w przypiętych wersjach i sprawdzane sumą SHA256.
- Jedyny niepubliczny składnik to oficjalne Steamworks SDK od Valve (licencja zabrania jego
  publikowania). Workflow sprawdza jego sumę SHA256 z [`cmake/steamworks-sdk.sha256`](cmake/steamworks-sdk.sha256)
  i przerywa budowanie, jeśli plik jest inny.

## Jak to jest testowane

Wszystkie poniższe testy uruchamia `ctest --test-dir build`, a GitHub Actions powtarza je przy każdej
zmianie w repozytorium – także w wersji z AddressSanitizer i UndefinedBehaviorSanitizer, które
przerywają test przy każdym błędzie pamięci lub niezdefiniowanym zachowaniu.

- **`tests/test_security.cpp`** – scenariusze ataków na logikę sieci (moduł `src/core/router.cpp`,
  tej samej, której używa program): osoba bez klucza, odtworzenie cudzego dowodu, obcy spoza lobby,
  przejęcie cudzego adresu, podszywanie się pod innych członków, router domowy i localhost, próby
  dostania się przez Ciebie do sieci domowej i internetu, dostęp do usług komputera, wycieki przez
  rozgłaszanie, kolizje adresów, uszkodzone i za duże pakiety, zalewanie wiadomościami HELLO.
- **`tests/fuzz_core.cpp`** – miliony losowych i celowo zepsutych pakietów, wiadomości, kodów sieci
  i nicków podawanych do wszystkich funkcji, które czytają dane od innych osób.
- **`tests/test_helper.sh`** – złośliwe argumenty dla pomocnika działającego jako root: wstrzykiwanie
  poleceń (`;`, `$(...)`, nowa linia), adresy spoza sieci, zapis ósemkowy, uid roota i kont systemowych.
- **`tests/test_core.cpp`** – filtr usług (w tym sztuczki z fragmentacją IP), czyszczenie tekstu,
  kody sieci, dowody i znaczniki.
- **Utwardzenie programu** – kompilacja z ochroną stosu, PIE, pełnym RELRO i niewykonywalnym stosem.

### Testy na dwóch komputerach

Ataków przez prawdziwą sieć Steam nie da się sprawdzić na jednym komputerze. Do tego służy wersja
konsolowa zbudowana w trybie testów:

```sh
cmake -B build-test -DKETTLELAN_SECURITY_TESTS=ON -DKETTLELAN_BUILD_GUI=OFF
cmake --build build-test --parallel
./build-test/kettlelan-cli
```

- `test-wrong-key <kod>` – wchodzi do lobby sieci z fałszywym dowodem klucza. Druga strona powinna
  zapisać w dzienniku „does not know the network key – ignoring”, a ping do niej nie może działać.
- `test-spoof <adres>` – wysyła członkom ping z podrobionym adresem nadawcy. Druga strona powinna
  zapisać „with a forged source address (spoofing)” i zwiększyć licznik zablokowanych prób.

Te polecenia nigdy nie trafiają do zwykłych paczek.

