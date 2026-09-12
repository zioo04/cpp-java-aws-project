package arena;

import com.amazonaws.services.lambda.runtime.Context;
import com.amazonaws.services.lambda.runtime.RequestHandler;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import software.amazon.awssdk.http.urlconnection.UrlConnectionHttpClient;
import software.amazon.awssdk.services.dynamodb.DynamoDbClient;
import software.amazon.awssdk.services.dynamodb.model.AttributeValue;
import software.amazon.awssdk.services.dynamodb.model.PutItemRequest;
import software.amazon.awssdk.services.dynamodb.model.ScanRequest;
import software.amazon.awssdk.services.dynamodb.model.ScanResponse;

import java.time.Instant;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

/**
 * Leaderboard service behind a Lambda Function URL.
 *
 *   GET  /scores  -> top ten, highest first
 *   POST /scores  -> {"name": "...", "score": 1400, "level": 3}
 *
 * The browser computes its own score, so a submitted score cannot be trusted.
 * Every submission is checked against the maximum the reported level could
 * possibly have produced. Those ceilings come from the C++ solver in engine/,
 * which enumerates what each level can spawn and what it is worth.
 */
public class ScoreHandler implements RequestHandler<Map<String, Object>, Map<String, Object>> {

    private static final String TABLE =
            System.getenv().getOrDefault("TABLE_NAME", "tank-scores");

    private static final int MAX_NAME_LENGTH = 10;
    private static final int MAX_LEVEL = 50;
    private static final int TOP_N = 10;

    private static final ObjectMapper JSON = new ObjectMapper();

    // Built once per container, reused by every warm invocation.
    private static final DynamoDbClient DB = DynamoDbClient.builder()
            .httpClient(UrlConnectionHttpClient.create())
            .build();

    @Override
    public Map<String, Object> handleRequest(Map<String, Object> event, Context context) {
        try {
            String method = httpMethod(event);

            if ("OPTIONS".equals(method)) return respond(204, "");
            if ("GET".equals(method)) return respond(200, topScores());
            if ("POST".equals(method)) return handleSubmit(event);

            return error(405, "use GET or POST");
        } catch (IllegalArgumentException rejected) {
            return error(400, rejected.getMessage());
        } catch (Exception failed) {
            if (context != null) context.getLogger().log("failed: " + failed);
            return error(500, "something went wrong on our side");
        }
    }

    // ------------------------------------------------------------ routing

    @SuppressWarnings("unchecked")
    private String httpMethod(Map<String, Object> event) {
        Object requestContext = event.get("requestContext");
        if (requestContext instanceof Map<?, ?> rc) {
            Object http = ((Map<String, Object>) rc).get("http");
            if (http instanceof Map<?, ?> h) {
                Object method = ((Map<String, Object>) h).get("method");
                if (method != null) return method.toString().toUpperCase();
            }
        }
        return "GET";
    }

    private Map<String, Object> handleSubmit(Map<String, Object> event) throws Exception {
        Object rawBody = event.get("body");
        if (rawBody == null) throw new IllegalArgumentException("body is missing");

        JsonNode body = JSON.readTree(rawBody.toString());
        String name = cleanName(body.path("name").asText(""));
        int score = body.path("score").asInt(-1);
        int level = body.path("level").asInt(0);

        validate(score, level);

        Map<String, AttributeValue> item = new HashMap<>();
        item.put("id", AttributeValue.fromS(UUID.randomUUID().toString()));
        item.put("playerName", AttributeValue.fromS(name));
        item.put("score", AttributeValue.fromN(Integer.toString(score)));
        item.put("level", AttributeValue.fromN(Integer.toString(level)));
        item.put("createdAt", AttributeValue.fromS(Instant.now().toString()));

        DB.putItem(PutItemRequest.builder().tableName(TABLE).item(item).build());

        return respond(201, topScores());
    }

    // --------------------------------------------------------- validation

    /**
     * Highest score reachable by the end of the given level.
     *
     * Each level spawns min(1 + level, 6) enemies. The most valuable enemy is
     * worth 350, plus 50 if a mine finishes it, so 400 is the per-enemy cap.
     * Clearing a level pays 200 x level.
     */
    static long scoreCeiling(int level) {
        long total = 0;
        for (int l = 1; l <= level; l++) {
            long enemies = Math.min(1 + l, 6);
            total += enemies * 400L;
            total += 200L * l;
        }
        return total;
    }

    private void validate(int score, int level) {
        if (score < 0) throw new IllegalArgumentException("score cannot be negative");
        if (level < 1 || level > MAX_LEVEL)
            throw new IllegalArgumentException("level must be between 1 and " + MAX_LEVEL);

        long ceiling = scoreCeiling(level);
        if (score > ceiling)
            throw new IllegalArgumentException(
                    "score " + score + " is above the " + ceiling + " ceiling for level " + level);
    }

    private String cleanName(String raw) {
        String trimmed = raw.trim().replaceAll("[\\p{Cntrl}<>]", "");
        if (trimmed.isEmpty()) trimmed = "PLAYER";
        return trimmed.length() > MAX_NAME_LENGTH
                ? trimmed.substring(0, MAX_NAME_LENGTH)
                : trimmed;
    }

    // ------------------------------------------------------------ reading

    /**
     * Scans the table and sorts in memory. Fine while the leaderboard is small;
     * a partitioned table with a sort key on score would be the move once it is not.
     */
    private String topScores() throws Exception {
        ScanResponse scan = DB.scan(ScanRequest.builder().tableName(TABLE).build());

        List<Map<String, AttributeValue>> rows = scan.items().stream()
                .sorted(Comparator.comparingInt(
                        (Map<String, AttributeValue> row) -> number(row, "score")).reversed())
                .limit(TOP_N)
                .toList();

        ArrayNode out = JSON.createArrayNode();
        for (Map<String, AttributeValue> row : rows) {
            ObjectNode entry = out.addObject();
            entry.put("name", text(row, "playerName"));
            entry.put("score", number(row, "score"));
            entry.put("level", number(row, "level"));
        }
        return JSON.writeValueAsString(out);
    }

    private static String text(Map<String, AttributeValue> row, String key) {
        AttributeValue v = row.get(key);
        return v == null || v.s() == null ? "PLAYER" : v.s();
    }

    private static int number(Map<String, AttributeValue> row, String key) {
        AttributeValue v = row.get(key);
        if (v == null || v.n() == null) return 0;
        try {
            return Integer.parseInt(v.n());
        } catch (NumberFormatException ignored) {
            return 0;
        }
    }

    // ----------------------------------------------------------- response

    private Map<String, Object> respond(int status, String body) {
        Map<String, String> headers = new HashMap<>();
        headers.put("Content-Type", "application/json");
        headers.put("Access-Control-Allow-Origin", "*");
        headers.put("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
        headers.put("Access-Control-Allow-Headers", "Content-Type");

        Map<String, Object> response = new HashMap<>();
        response.put("statusCode", status);
        response.put("headers", headers);
        response.put("body", body);
        return response;
    }

    private Map<String, Object> error(int status, String message) {
        ObjectNode node = JSON.createObjectNode();
        node.put("error", message);
        return respond(status, node.toString());
    }
}
