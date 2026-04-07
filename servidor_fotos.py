"""
Servidor HTTP que recebe fotos do ESP32-CAM via POST
e as salva na pasta 'fotos/' do repositório.

Como usar:
  python3 servidor_fotos.py

Depois copie o IP exibido para a variável 'servidorIP' no .ino
"""

import http.server
import os
import socket
from datetime import datetime

PASTA_FOTOS = os.path.join(os.path.dirname(__file__), "fotos")
PORTA = 5000

os.makedirs(PASTA_FOTOS, exist_ok=True)


def ip_local():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    finally:
        s.close()


class ManipuladorFoto(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/foto":
            self.send_response(404)
            self.end_headers()
            return

        tamanho = int(self.headers.get("Content-Length", 0))
        dados = self.rfile.read(tamanho)

        nome = datetime.now().strftime("foto_%Y%m%d_%H%M%S.jpg")
        caminho = os.path.join(PASTA_FOTOS, nome)

        with open(caminho, "wb") as f:
            f.write(dados)

        print(f"[OK] Foto salva: fotos/{nome}  ({len(dados)} bytes)")

        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"OK")

    def log_message(self, *args):
        pass  # silencia logs de requisição padrão


if __name__ == "__main__":
    ip = ip_local()
    print(f"Servidor iniciado em http://{ip}:{PORTA}")
    print(f'Coloque no .ino: const char* servidorIP = "http://{ip}:{PORTA}/foto";')
    print(f"Fotos serão salvas em: {PASTA_FOTOS}/\n")

    httpd = http.server.HTTPServer(("0.0.0.0", PORTA), ManipuladorFoto)
    httpd.serve_forever()