VERSION="1.0.0"
URL="https://cloudius.artifactoryonline.com/cloudius/mgmt-releases/com/cloudius/web/"
API="https://cloudius.artifactoryonline.com/cloudius/api/storage/mgmt-releases/com/cloudius/web/"
DEST=$VERSION
if [ ! -d $DEST ]; then
  echo "Downloading version $VERSION of mgmt"
  mkdir $DEST -p
  wget -O "$DEST/$VERSION.tar" "$URL/$VERSION/web-$VERSION.tar" 
  ASUM=`wget -q -O - "$API/$VERSION/web-$VERSION.tar" | ./jq '.checksums.md5' - `
  DSUM=`md5sum "$DEST/$VERSION.tar" | awk '{print $1}' `
  if [ $ASUM != "\"$DSUM\"" ]; then
    echo "Failed to validate download checksum!"
    exit 1
  fi
  tar -C $DEST -xf "$DEST/$VERSION.tar" 
else
  echo "$VERSION of mgmt seems in place skipping .."
fi
