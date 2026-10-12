# PROGRESS

## Résumé

Tout le code des steps 0 à 7 est écrit et compile sans warning.
**Aucun programme BPF n'a été chargé dans le noyau** : `sudo -n` échoue
(« sudo: interactive authentication is required »). La partie noyau est donc
NON VERIFIEE tant que `scripts/manual-tests.sh` n'a pas été lancé.

| Step | Code | Vérifié par l'agent | À lancer à la main |
|---|---|---|---|
| 0 Fondations | fait | `make clean && make` OK | `bash scripts/manual-tests.sh 0` |
| 1 Connexions | fait | compilation seulement | `bash scripts/manual-tests.sh 1` |
| 2 Retransmissions | fait | compilation seulement | `bash scripts/manual-tests.sh 2` |
| 3 Latence | fait | compilation seulement | `bash scripts/manual-tests.sh 3` |
| 4 Exporter | fait | `/metrics` OK en mode démo (données factices) | `bash scripts/manual-tests.sh 4` |
| 5 Grafana | fait | syntaxe YAML/JSON seulement | `bash scripts/manual-tests.sh 5` |
| 6 Benchmarks | fait | colonne « sans traceur » seulement | `bash scripts/manual-tests.sh 6` |
| 7 README | fait | - | à relire après les tests manuels |

Ordre conseillé : 0, 1, 2, 3, 4, puis 6, puis 5. Si le step 0 échoue (rejet
par le vérifieur BPF), les suivants échoueront aussi : l'erreur de libbpf
indique le programme et la ligne fautifs.

## À faire par l'utilisateur
- `sudo apt install make` : **make n'est pas installé** sur la machine. Le
  Makefile a été vérifié avec une copie temporaire de GNU Make 4.4.1 (paquet
  téléchargé avec `apt download` et extrait dans un dossier temporaire hors
  du projet, rien d'installé sur le système).
- `git init` + commits : non faits (demande de l'utilisateur).
- Lancer les tests manuels ci-dessus, puis corriger README/PROGRESS selon le
  résultat.

## Paquets installés
Aucun.

## Blocages
- Pas de sudo sans mot de passe : aucun test noyau possible.
- `make`, `docker`, `iperf3`, `promtool` absents.
- `/sys/kernel/tracing` illisible sans root : l'existence des tracepoints
  `tcp:tcp_retransmit_skb` et `sock:inet_sock_set_state` est déduite du BTF
  (`struct trace_event_raw_*` présentes) et de /proc/kallsyms, pas confirmée
  par un attachement réel.

## Détail par step

### Step 0 - Fondations
- Fait : arborescence, Makefile (vmlinux.h -> objet BPF -> skeleton ->
  binaire, tout dans `build/`), .gitignore, CLAUDE.md, README.
- Vérifié : build complet depuis un état propre ; sans root, `./netobserve`
  échoue proprement avec EPERM.
- NON VERIFIE : chargement en root.

### Step 1 - Connexions sortantes
- Fait : kprobe `tcp_v4_connect` (symbole présent dans /proc/kallsyms) ->
  ring buffer (pid, comm, IP/port dest, timestamp) ; une ligne par événement,
  compteur par nom de processus affiché à l'arrêt.
- NON VERIFIE : vérifieur BPF, attachement, exactitude des événements.

### Step 2 - Retransmissions
- Fait : tracepoint `tcp:tcp_retransmit_skb`, adresses/ports source et dest,
  compteur par IP de destination. pid/comm fournis mais peu fiables (le
  processus courant n'est en général pas le propriétaire de la socket).
- Fait : `scripts/gen-retrans.sh` (netns + paire veth + `tc netem loss 20%`,
  nettoyage automatique). `CONFIG_NET_SCH_NETEM=m` et le module existe dans
  /lib/modules.
- NON VERIFIE : tout (le script demande root).

### Step 3 - Latence de connexion
- Fait : début mémorisé dans le kprobe (map LRU indexée par l'adresse de la
  socket), fin sur `sock:inet_sock_set_state` -> ESTABLISHED ; histogramme
  log2 en microsecondes (22 cases) + somme, dans des maps BPF.
- NON VERIFIE : plausibilité des valeurs.

### Step 4 - Exporter Prometheus
- Fait : serveur HTTP minimal (sockets), `/metrics` : `connections_total{comm}`,
  `tcp_retransmits_total{dst}`, histogramme `connect_latency_seconds`.
  Cardinalité limitée aux 32 premiers labels + `other` (voir `src/table.h`).
- Vérifié : `./netobserve -d` (données FACTICES, sans BPF) puis
  `curl localhost:<port>/metrics` : en-têtes HTTP, HELP/TYPE, échappement des
  labels, buckets cumulatifs, `other`, 404 hors `/metrics`. Format relu à
  l'œil (pas de `promtool`).
- NON VERIFIE : avec des données réelles ; scrape par un vrai Prometheus.

### Step 5 - Grafana + Prometheus
- Fait : `dashboards/docker-compose.yml`, `prometheus.yml`, provisioning
  Grafana, dashboard JSON (4 panneaux). Note OpenTelemetry dans le README.
- Vérifié : syntaxe YAML et JSON uniquement.
- NON VERIFIE : docker absent. Point à surveiller : la cible
  `host.docker.internal:9400` (peut dépendre de Docker Desktop vs docker
  natif dans WSL).

### Step 6 - Benchmarks
- Fait : `bench/connspam.c` (rafale de connexions loopback), `bench/run.sh`
  (N runs alternés sans/avec traceur, moyenne ± écart-type, table markdown).
- Vérifié : `SKIP_TRACER=1 bash bench/run.sh` -> `bench/RESULTS.md` avec la
  colonne de base (~18 600 connexions/s, ~36 us en moyenne).
- NON VERIFIE : la branche « avec traceur » du script et donc le surcoût.

### Step 7 - README
- Fait. Il indique en tête que la partie noyau n'a pas été exécutée et que
  le benchmark ne contient que la base. À mettre à jour après les tests.
