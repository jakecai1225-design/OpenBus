import urllib.request
import os

os.makedirs('third_party/qcustomplot', exist_ok=True)
url = 'https://raw.githubusercontent.com/dbzhang800/QCustomPlot/master/qcustomplot.h'
path = 'third_party/qcustomplot/qcustomplot.h'

print(f'Downloading qcustomplot...')
urllib.request.urlretrieve(url, path)
print(f'Done: {path}')
