import tempfile,subprocess,sys
with tempfile.TemporaryDirectory() as folder:
 subprocess.run([sys.argv[1],folder],check=True)
