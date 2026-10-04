package koral.compose

import androidx.compose.runtime.Composable

// Icons, as Compose's material-icons-core has them: `Icon(Icons.Default.Add, contentDescription = "Add")`.
// Each is drawn — lines and circles on a grid of 24, in the colour it is tinted — not loaded: there are no
// image files. The set is the ones a tools interface reaches for; Filled, Outlined, Rounded and Sharp are
// the same drawings.

/** A drawing on a grid of 24 by 24, scaled to whatever size it is shown at. */
class ImageVector internal constructor(val name: String, internal val draw: Pen.() -> Unit)

/** What an icon is drawn with: lines of one width and colour, in the grid's units. */
class Pen internal constructor(private val scope: DrawScope, private val unit: Float, private val color: Color) {
    private val width = 2f * unit
    private fun at(x: Float, y: Float) = Offset(x * unit, y * unit)
    fun line(x1: Float, y1: Float, x2: Float, y2: Float) = scope.drawLine(color, at(x1, y1), at(x2, y2), width, StrokeCap.Round)
    fun path(vararg points: Float, close: Boolean = false) {
        for (i in 0 until points.size / 2 - 1) line(points[i * 2], points[i * 2 + 1], points[i * 2 + 2], points[i * 2 + 3])
        if (close && points.size >= 4) line(points[points.size - 2], points[points.size - 1], points[0], points[1])
    }
    fun circle(x: Float, y: Float, radius: Float) = scope.drawCircle(color, radius * unit, at(x, y), style = Stroke(width))
    fun dot(x: Float, y: Float, radius: Float = 1.5f) = scope.drawCircle(color, radius * unit, at(x, y))
    fun arc(x: Float, y: Float, radius: Float, start: Float, sweep: Float) =
        scope.drawArc(color, start, sweep, useCenter = false, topLeft = at(x - radius, y - radius), size = Size(2f * radius * unit, 2f * radius * unit),
                      style = Stroke(width, cap = StrokeCap.Round))
    fun box(left: Float, top: Float, right: Float, bottom: Float, corner: Float = 1.5f) =
        scope.drawRoundRect(color, at(left, top), Size((right - left) * unit, (bottom - top) * unit), CornerRadius(corner * unit), style = Stroke(width))
}

/** An [imageVector], [tint]ed — the content colour around it, unless given — at 24 units unless [modifier] says. */
@Composable
fun Icon(imageVector: ImageVector, contentDescription: String?, modifier: Modifier = Modifier, tint: Color = LocalContentColor.current) {
    @Suppress("UNUSED_VARIABLE") val described = contentDescription
    val color = if (tint.isSpecified) tint else LocalTheme.current.text
    Canvas(Modifier.size(24.dp).then(modifier)) { imageVector.draw(Pen(this, size.minDimension / 24f, color)) }
}

/** The icons, by name. */
object Icons {
    val Default = Set
    val Filled = Set
    val Outlined = Set
    val Rounded = Set
    val Sharp = Set
    object AutoMirrored { val Filled = Set; val Default = Set; val Outlined = Set; val Rounded = Set }

    object Set {
        val Add = ImageVector("Add") { line(12f, 5f, 12f, 19f); line(5f, 12f, 19f, 12f) }
        val Close = ImageVector("Close") { line(6f, 6f, 18f, 18f); line(18f, 6f, 6f, 18f) }
        val Clear = Close
        val Check = ImageVector("Check") { path(5f, 12.5f, 10f, 17.5f, 19f, 7f) }
        val Done = Check
        val Menu = ImageVector("Menu") { line(4f, 7f, 20f, 7f); line(4f, 12f, 20f, 12f); line(4f, 17f, 20f, 17f) }
        val MoreVert = ImageVector("MoreVert") { dot(12f, 6f); dot(12f, 12f); dot(12f, 18f) }
        val MoreHoriz = ImageVector("MoreHoriz") { dot(6f, 12f); dot(12f, 12f); dot(18f, 12f) }
        val ArrowBack = ImageVector("ArrowBack") { line(20f, 12f, 5f, 12f); path(11f, 6f, 5f, 12f, 11f, 18f) }
        val ArrowForward = ImageVector("ArrowForward") { line(4f, 12f, 19f, 12f); path(13f, 6f, 19f, 12f, 13f, 18f) }
        val ArrowUpward = ImageVector("ArrowUpward") { line(12f, 20f, 12f, 5f); path(6f, 11f, 12f, 5f, 18f, 11f) }
        val ArrowDownward = ImageVector("ArrowDownward") { line(12f, 4f, 12f, 19f); path(6f, 13f, 12f, 19f, 18f, 13f) }
        val ArrowDropDown = ImageVector("ArrowDropDown") { path(7f, 10f, 12f, 15f, 17f, 10f) }
        val KeyboardArrowDown = ArrowDropDown
        val KeyboardArrowUp = ImageVector("KeyboardArrowUp") { path(7f, 14f, 12f, 9f, 17f, 14f) }
        val KeyboardArrowLeft = ImageVector("KeyboardArrowLeft") { path(14f, 7f, 9f, 12f, 14f, 17f) }
        val KeyboardArrowRight = ImageVector("KeyboardArrowRight") { path(10f, 7f, 15f, 12f, 10f, 17f) }
        val Search = ImageVector("Search") { circle(10.5f, 10.5f, 6f); line(15f, 15f, 20f, 20f) }
        val Settings = ImageVector("Settings") {
            circle(12f, 12f, 3f)
            for (i in 0 until 8) {
                val a = Math.toRadians(i * 45.0); val c = Math.cos(a).toFloat(); val s = Math.sin(a).toFloat()
                line(12f + c * 6.5f, 12f + s * 6.5f, 12f + c * 9f, 12f + s * 9f)
            }
            circle(12f, 12f, 6.5f)
        }
        val Home = ImageVector("Home") { path(4f, 11f, 12f, 4f, 20f, 11f); path(6f, 10f, 6f, 20f, 18f, 20f, 18f, 10f); path(10f, 20f, 10f, 14f, 14f, 14f, 14f, 20f) }
        val Delete = ImageVector("Delete") { line(5f, 7f, 19f, 7f); path(9f, 7f, 9f, 4f, 15f, 4f, 15f, 7f); path(7f, 7f, 8f, 20f, 16f, 20f, 17f, 7f); line(10f, 11f, 10f, 16f); line(14f, 11f, 14f, 16f) }
        val Edit = ImageVector("Edit") { path(5f, 19f, 5f, 15f, 15f, 5f, 19f, 9f, 9f, 19f, close = true); line(13f, 7f, 17f, 11f) }
        val Create = Edit
        val Info = ImageVector("Info") { circle(12f, 12f, 9f); line(12f, 11f, 12f, 17f); dot(12f, 7.5f, 1.2f) }
        val Warning = ImageVector("Warning") { path(12f, 4f, 21f, 20f, 3f, 20f, close = true); line(12f, 10f, 12f, 14.5f); dot(12f, 17.2f, 1.1f) }
        val Star = ImageVector("Star") {
            val points = FloatArray(20)
            for (i in 0 until 10) {
                val r = if (i % 2 == 0) 9f else 4f
                val a = Math.toRadians(-90.0 + i * 36.0)
                points[i * 2] = 12f + Math.cos(a).toFloat() * r; points[i * 2 + 1] = 12.5f + Math.sin(a).toFloat() * r
            }
            path(*points, close = true)
        }
        val Favorite = ImageVector("Favorite") { arc(8f, 9f, 4f, 180f, 180f); arc(16f, 9f, 4f, 180f, 180f); path(4f, 9f, 5f, 13f, 12f, 20f, 19f, 13f, 20f, 9f) }
        val FavoriteBorder = Favorite
        val Refresh = ImageVector("Refresh") { arc(12f, 12f, 7.5f, -60f, 300f); path(16f, 3f, 16.5f, 6.5f, 13f, 7f) }
        val PlayArrow = ImageVector("PlayArrow") { path(8f, 5f, 19f, 12f, 8f, 19f, close = true) }
        val Person = ImageVector("Person") { circle(12f, 8f, 4f); arc(12f, 21f, 8f, 180f, 180f) }
        val AccountCircle = ImageVector("AccountCircle") { circle(12f, 12f, 9f); circle(12f, 10f, 3f); arc(12f, 20f, 6f, 200f, 140f) }
        val Lock = ImageVector("Lock") { box(6f, 11f, 18f, 20f); arc(12f, 11f, 4f, 180f, 180f); dot(12f, 15.5f, 1.3f) }
        val Email = ImageVector("Email") { box(3f, 6f, 21f, 18f); path(3.5f, 7f, 12f, 13f, 20.5f, 7f) }
        val MailOutline = Email
        val Notifications = ImageVector("Notifications") { path(6f, 17f, 6f, 11f); arc(12f, 11f, 6f, 180f, 180f); path(18f, 11f, 18f, 17f); line(4f, 17f, 20f, 17f); arc(12f, 18.5f, 2f, 0f, 180f) }
        val Share = ImageVector("Share") { dot(18f, 6f, 2.2f); dot(6f, 12f, 2.2f); dot(18f, 18f, 2.2f); line(8f, 11f, 16f, 7f); line(8f, 13f, 16f, 17f) }
        val List = ImageVector("List") { dot(5f, 7f, 1.1f); dot(5f, 12f, 1.1f); dot(5f, 17f, 1.1f); line(9f, 7f, 20f, 7f); line(9f, 12f, 20f, 12f); line(9f, 17f, 20f, 17f) }
        val DateRange = ImageVector("DateRange") { box(4f, 6f, 20f, 20f); line(4f, 10f, 20f, 10f); line(8f, 4f, 8f, 7f); line(16f, 4f, 16f, 7f) }
        val Build = ImageVector("Build") { circle(8f, 8f, 3.5f); line(10.5f, 10.5f, 19f, 19f) }
        val Call = ImageVector("Call") { path(6f, 4f, 9f, 4f, 10.5f, 8f, 8.5f, 10f, 14f, 15.5f, 16f, 13.5f, 20f, 15f, 20f, 18f, 17f, 20f, 9f, 16f, 4f, 7f, close = true) }
        val Phone = Call
        val Place = ImageVector("Place") { arc(12f, 10f, 6f, 150f, 240f); path(6.8f, 13f, 12f, 21f, 17.2f, 13f); dot(12f, 10f, 2f) }
        val LocationOn = Place
        val ExitToApp = ImageVector("ExitToApp") { path(13f, 4f, 20f, 4f, 20f, 20f, 13f, 20f); line(4f, 12f, 14f, 12f); path(10f, 8f, 14f, 12f, 10f, 16f) }
        val Send = ImageVector("Send") { path(4f, 5f, 20f, 12f, 4f, 19f, 6f, 12f, close = true); line(6f, 12f, 12f, 12f) }
        val ShoppingCart = ImageVector("ShoppingCart") { path(3f, 5f, 6f, 5f, 8f, 15f, 18f, 15f, 20f, 8f, 7f, 8f); dot(9f, 19f, 1.5f); dot(17f, 19f, 1.5f) }
        val ThumbUp = ImageVector("ThumbUp") { box(4f, 11f, 8f, 20f); path(8f, 11f, 12f, 4f, 14f, 6f, 13f, 10f, 20f, 10f, 18f, 20f, 8f, 20f) }
        val AddCircle = ImageVector("AddCircle") { circle(12f, 12f, 9f); line(12f, 7.5f, 12f, 16.5f); line(7.5f, 12f, 16.5f, 12f) }
        val CheckCircle = ImageVector("CheckCircle") { circle(12f, 12f, 9f); path(7.5f, 12.5f, 10.5f, 15.5f, 16.5f, 9f) }
        val Face = ImageVector("Face") { circle(12f, 12f, 9f); dot(9f, 10f, 1.2f); dot(15f, 10f, 1.2f); arc(12f, 12.5f, 4.5f, 30f, 120f) }
        val AccountBox = ImageVector("AccountBox") { box(4f, 4f, 20f, 20f); circle(12f, 10f, 3f); arc(12f, 20f, 5.5f, 205f, 130f) }
    }
}
