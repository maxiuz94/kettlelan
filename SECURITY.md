# Bezpieczeństwo KettleLAN

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
  który umie wyłącznie utworzyć lub usunąć kartę `kettle0` i ściśle sprawdza argumenty.
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
  działający na Twoim koncie mógłby go podmienić. Paczka `.deb` instaluje pomocnika w katalogu
  systemowym należącym do roota.

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

- `test-zly-klucz <kod>` – wchodzi do lobby sieci z fałszywym dowodem klucza. Druga strona powinna
  zapisać w dzienniku „nie zna klucza sieci – ignoruję”, a ping do niej nie może działać.
- `test-podszyj <adres>` – wysyła członkom ping z podrobionym adresem nadawcy. Druga strona powinna
  zapisać „z cudzym adresem nadawcy (podszywanie się)” i zwiększyć licznik zablokowanych prób.

Te polecenia nigdy nie trafiają do zwykłych paczek.

---

**English summary:** KettleLAN runs unprivileged (only a tiny, strictly validated helper creates the
`kettle0` interface). By default it relays all traffic through Valve's servers so peers never learn your
public IP, blocks inbound access to common system services (SSH, SMB, RDP, databases…) and stops your
system's service announcements (mDNS, NetBIOS, SSDP…) from reaching peers. Only peers that prove
knowledge of the network key can exchange packets. Report vulnerabilities privately via
**Security → Report a vulnerability**.
