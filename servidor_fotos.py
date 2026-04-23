"""
Servidor HTTP para receber fotos do ESP32-CAM.

Como usar:
  python3 servidor_fotos.py

O servidor abre a porta 5000 e salva cada POST recebido em /foto dentro da
pasta "fotos" ao lado deste arquivo.
"""

from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import socket


PORTA = 5000
PASTA_FOTOS = Path(__file__).resolve().parent / "fotos"


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


class ServidorFotos(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/":
            self.send_error(404, "Use POST /foto para enviar uma imagem.")
            return

        mensagem = (
            "Servidor de fotos ativo.\n"
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
        agora = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        nome_arquivo = f"foto_{agora}.jpg"
        caminho = PASTA_FOTOS / nome_arquivo
        caminho.write_bytes(dados)

        print(f"[OK] Foto salva em {caminho} ({len(dados)} bytes)")

        resposta = nome_arquivo.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(resposta)))
        self.end_headers()
        self.wfile.write(resposta)

    def log_message(self, formato, *args):
        print(f"[HTTP] {self.client_address[0]} - {formato % args}")


def main():
    PASTA_FOTOS.mkdir(exist_ok=True)
    ip = descobrir_ip_local()
    url = f"http://{ip}:{PORTA}/foto"

    print(f"Servidor iniciado em: http://{ip}:{PORTA}")
    print(f'Copie para o .ino: const char* SERVIDOR_FOTOS_URL = "{url}";')
    print(f"Fotos serao salvas em: {PASTA_FOTOS}")
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
