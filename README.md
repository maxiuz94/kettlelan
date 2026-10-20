# KettleLAN

**Otwartoźródłowy wirtualny LAN przez sieć Steam** – zagraj ze znajomymi w gry LAN (Quake 3,
Unreal Tournament, Counter-Strike 1.6, Warcraft III…) tak, jakbyście siedzieli przy jednym routerze.
Bez limitu osób, bez przekierowywania portów i bez własnego serwera.

> Status: **etap 1 – wersja konsolowa dla Linuksa.** Windows i interfejs graficzny są w drodze.

## Jak to działa

- Tworzysz sieć i dostajesz kod, np. `KTL-7F3Q-9XM2-A4KD`. Wysyłasz go znajomym.
- Każdy uczestnik dostaje stały adres `100.x.x.x` na wirtualnej karcie sieciowej `kettle0`.
- Gry widzą zwykłą sieć lokalną: hostujesz serwer jak w LAN-ie, a znajomi łączą się na Twój adres.
- Ruch płynie przez sieć Steam (szyfrowany; połączenie bezpośrednie albo przez relay Valve),
  więc działa nawet za NAT-em operatora.
- Do sieci wpuszczani są tylko ci, którzy znają kod – sam numer lobby nie wystarczy.

KettleLAN używa testowego App ID `480` (Spacewar), dlatego w Steamie będziesz widoczny jako
„grający w Spacewar”.

## Budowanie (Linux)

Potrzebne pakiety:

| Dystrybucja    | Polecenie |
|----------------|-----------|
| Arch / Manjaro | `sudo pacman -S --needed base-devel cmake git polkit iproute2` |
| Ubuntu / Debian | `sudo apt install build-essential cmake git pkexec iproute2` |
| Fedora         | `sudo dnf install gcc-c++ cmake git polkit iproute` |

Potem:

```sh
git clone https://github.com/maxiuz94/kettlelan.git
cd kettlelan
./scripts/build.sh
```

Do kompilacji potrzebne jest **Steamworks SDK** (darmowe, wymaga konta Steam). Licencja Valve nie
pozwala dołączyć go do repozytorium. Skrypt `build.sh` sam zaproponuje otwarcie strony pobierania
i znajdzie plik `steamworks_sdk_XXX.zip` w folderze Pobrane. Gotowe paczki KettleLAN SDK nie
wymagają.

## Uruchamianie

1. Włącz Steama i zaloguj się.
2. Uruchom `./build/kettlelan`. Przy pierwszym uruchomieniu (i po każdym restarcie komputera)
   system poprosi o hasło administratora – to tworzenie karty sieciowej `kettle0`.
   Sam program działa jako zwykły użytkownik.
3. Wpisz `utworz Wieczór z Quake` i wyślij znajomym pokazany kod
   albo dołącz do cudzej sieci: `dolacz KTL-XXXX-XXXX-XXXX`.
4. `lista` pokazuje członków, ich adresy, ping i rodzaj połączenia.

Karta `kettle0` zostaje do restartu komputera. Usunięcie ręczne:
`pkexec ./build/kettlelan-helper down`.

## Bezpieczeństwo i prywatność

- Program nie działa jako root. Uprawnienia administratora ma wyłącznie krótki skrypt
  [`scripts/kettlelan-helper`](scripts/kettlelan-helper), który tylko tworzy lub usuwa kartę
  `kettle0` i ściśle sprawdza argumenty.
- Pakiety przyjmowane są wyłącznie od zweryfikowanych członków sieci i tylko z ich własnego adresu.
- KettleLAN nie wysyła żadnych danych poza siecią Steam i nie zbiera statystyk.

## Licencja

BSD-3-Clause, zobacz [LICENSE](LICENSE). Projekt bazuje na
[PartyLAN](https://github.com/gyf304/partylan) autorstwa Yifan Gu.

Steam jest znakiem towarowym Valve Corporation. KettleLAN nie jest powiązany z Valve.

---

**English:** KettleLAN is an open-source virtual LAN over the Steam network for playing classic LAN
games with friends – no player limit, no port forwarding, no server. Build with
`./scripts/build.sh` (requires the free Steamworks SDK). Licensed under BSD-3-Clause.
