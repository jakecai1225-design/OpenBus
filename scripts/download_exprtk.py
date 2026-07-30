import urllib.request
import os

os.makedirs('third_party/exprtk', exist_ok=True)

url = 'https://raw.githubusercontent.com/ArashPartow/exprtk/master/exprtk.hpp'
exprtk_path = 'third_party/exprtk/exprtk.hpp'

print(f'Downloading exprtk...')
urllib.request.urlretrieve(url, exprtk_path)
print(f'Done: {exprtk_path}')
