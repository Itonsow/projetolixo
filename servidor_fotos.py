"""
Servidor HTTP para receber fotos do ESP32-CAM, classificar com YOLO e responder
ao ESP com a classe detectada.

Como usar:
  1. Coloque seu arquivo .pt treinado na raiz deste projeto.
  2. Instale as dependencias:
       python3 -m pip install -r requirements.txt
  3. Execute:
       python3 servidor_fotos.py

O servidor:
  - recebe imagens JPEG em POST /foto;
  - salva a imagem original em fotos/;
  - roda inferencia com o primeiro .pt encontrado na raiz do projeto;
  - salva a imagem com bounding boxes em results/;
  - responde ao ESP com: plastico, metal, papel, biologico ou desconhecido.
"""

from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import socket
import threading
import unicodedata

try:
    from ultralytics import YOLO
except ImportError:
    YOLO = None


PORTA = 5000
RAIZ_PROJETO = Path(__file__).resolve().parent
PASTA_FOTOS = RAIZ_PROJETO / "fotos"
PASTA_RESULTS = RAIZ_PROJETO / "results"
CONFIANCA_MINIMA = 0.25

modelo = None
modelo_lock = threading.Lock()


def descobrir_ip_local() -> str:
    """Descobre o IP da maquina na rede local."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        sock.close()


def encontrar_modelo_pt() -> Path:
    modelos = sorted(RAIZ_PROJETO.glob("*.pt"))
    if not modelos:
        raise FileNotFoundError(
            f"Nenhum arquivo .pt encontrado em {RAIZ_PROJETO}. "
            "Coloque seu modelo YOLO treinado na raiz do projeto."
        )
    return modelos[0]


def carregar_modelo():
    if YOLO is None:
        raise RuntimeError(
            "Pacote ultralytics nao instalado. Rode: "
            "python3 -m pip install -r requirements.txt"
        )

    caminho_modelo = encontrar_modelo_pt()
    print(f"[YOLO] Carregando modelo: {caminho_modelo}")
    return YOLO(str(caminho_modelo))


def normalizar_texto(texto: str) -> str:
    texto = unicodedata.normalize("NFKD", texto)
    texto = "".join(char for char in texto if not unicodedata.combining(char))
    texto = texto.lower().strip()
    return re.sub(r"[^a-z0-9]+", "", texto)


def classe_padrao(nome_classe: str) -> str:
    nome = normalizar_texto(nome_classe)

    aliases = {
        "plastico": "plastico",
        "plastical": "plastico",
        "metal": "metal",
        "papel": "papel",
        "paper": "papel",
        "biologico": "biologico",
        "bio": "biologico",
        "biological": "biologico",
        "organic": "biologico",
        "organico": "biologico",
    }

    return aliases.get(nome, "desconhecido")


def inferir_lixo(caminho_imagem: Path, caminho_resultado: Path) -> tuple[str, float]:
    with modelo_lock:
        resultados = modelo(str(caminho_imagem), conf=CONFIANCA_MINIMA, verbose=False)

    resultado = resultados[0]
    resultado.save(filename=str(caminho_resultado))

    if resultado.boxes is None or len(resultado.boxes) == 0:
        return "desconhecido", 0.0

    indice_melhor = int(resultado.boxes.conf.argmax().item())
    confianca = float(resultado.boxes.conf[indice_melhor].item())
    id_classe = int(resultado.boxes.cls[indice_melhor].item())
    nome_classe = resultado.names[id_classe]

    return classe_padrao(nome_classe), confianca


class ServidorFotos(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/":
            self.send_error(404, "Use POST /foto para enviar uma imagem.")
            return

        mensagem = (
            "Servidor de fotos e inferencia ativo.\n"
            "Envie imagens JPEG com POST /foto.\n"
        ).encode("utf-8")

        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(mensagem)))
        self.end_headers()
        self.wfile.write(mensagem)

    def do_POST(self):
        if self.path != "/foto":
            self.send_error(404, "Rota nao encontrada.")
            return

        tamanho = int(self.headers.get("Content-Length", "0"))
        if tamanho <= 0:
            self.send_error(400, "Requisicao sem imagem.")
            return

        dados = self.rfile.read(tamanho)
        if not dados:
            self.send_error(400, "Imagem vazia.")
            return

        PASTA_FOTOS.mkdir(exist_ok=True)
        PASTA_RESULTS.mkdir(exist_ok=True)

        agora = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        nome_arquivo = f"foto_{agora}.jpg"
        caminho_foto = PASTA_FOTOS / nome_arquivo
        caminho_resultado = PASTA_RESULTS / f"resultado_{agora}.jpg"
        caminho_foto.write_bytes(dados)

        try:
            classe, confianca = inferir_lixo(caminho_foto, caminho_resultado)
        except Exception as erro:
            print(f"[ERRO] Falha na inferencia: {erro}")
            self.send_error(500, f"Falha na inferencia: {erro}")
            return

        print(
            f"[OK] {nome_arquivo} -> {classe} "
            f"({confianca:.2f}) | resultado: {caminho_resultado.name}"
        )

        resposta = classe.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(resposta)))
        self.end_headers()
        self.wfile.write(resposta)

    def log_message(self, formato, *args):
        print(f"[HTTP] {self.client_address[0]} - {formato % args}")


def main():
    global modelo

    PASTA_FOTOS.mkdir(exist_ok=True)
    PASTA_RESULTS.mkdir(exist_ok=True)
    modelo = carregar_modelo()

    ip = descobrir_ip_local()
    url = f"http://{ip}:{PORTA}/foto"

    print(f"Servidor iniciado em: http://{ip}:{PORTA}")
    print(f'Copie para o .ino: const char* SERVIDOR_FOTOS_URL = "{url}";')
    print(f"Fotos originais serao salvas em: {PASTA_FOTOS}")
    print(f"Resultados com boxes serao salvos em: {PASTA_RESULTS}")
    print("Pressione Ctrl+C para encerrar.\n")

    servidor = ThreadingHTTPServer(("0.0.0.0", PORTA), ServidorFotos)
    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        print("\nServidor encerrado.")
    finally:
        servidor.server_close()


if __name__ == "__main__":
    main()
